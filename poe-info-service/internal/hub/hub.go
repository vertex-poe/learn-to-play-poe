package hub

import (
	"log"
	"sync"
)

const clientBufSize = 256

// Client represents a connected WebSocket client.
type Client struct {
	Send chan []byte
	done chan struct{}
}

func NewClient() *Client {
	return &Client{
		Send: make(chan []byte, clientBufSize),
		done: make(chan struct{}),
	}
}

// Close signals that this client is disconnecting. Safe to call once.
func (c *Client) Close() {
	close(c.done)
}

// Done returns a channel that is closed when the client disconnects.
func (c *Client) Done() <-chan struct{} {
	return c.done
}

// Hub manages topic-based subscriptions and fans out published messages.
type Hub struct {
	mu   sync.RWMutex
	subs map[string]map[*Client]struct{}
}

func New() *Hub {
	return &Hub{subs: make(map[string]map[*Client]struct{})}
}

func (h *Hub) Subscribe(c *Client, topic string) {
	h.mu.Lock()
	defer h.mu.Unlock()
	if h.subs[topic] == nil {
		h.subs[topic] = make(map[*Client]struct{})
	}
	h.subs[topic][c] = struct{}{}
}

func (h *Hub) Unsubscribe(c *Client, topic string) {
	h.mu.Lock()
	defer h.mu.Unlock()
	delete(h.subs[topic], c)
}

func (h *Hub) UnsubscribeAll(c *Client) {
	h.mu.Lock()
	defer h.mu.Unlock()
	for _, clients := range h.subs {
		delete(clients, c)
	}
}

// Publish delivers msg to all subscribers of topic. Slow clients are dropped,
// never allowed to stall fast ones.
func (h *Hub) Publish(topic string, msg []byte) {
	h.mu.RLock()
	targets := make([]*Client, 0, len(h.subs[topic]))
	for c := range h.subs[topic] {
		targets = append(targets, c)
	}
	h.mu.RUnlock()

	for _, c := range targets {
		select {
		case c.Send <- msg:
		case <-c.done:
			// client is disconnecting; skip
		default:
			log.Printf("hub: slow client on topic %q, event dropped", topic)
		}
	}
}
