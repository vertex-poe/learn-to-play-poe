#pragma once

#include <QObject>
#include <atomic>

class BackgroundWorker : public QObject
{
    Q_OBJECT
public:
    explicit BackgroundWorker(QObject *parent = nullptr) : QObject(parent) {}

    void cancel() { m_cancelled.store(true, std::memory_order_relaxed); }
    virtual void start() = 0;

signals:
    void progress(int percent, QString message);
    void finished();
    void failed(QString error);

protected:
    bool isCancelled() const { return m_cancelled.load(std::memory_order_relaxed); }

private:
    std::atomic<bool> m_cancelled{false};
};
