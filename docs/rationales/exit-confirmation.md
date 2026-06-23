<!-- docs/rationales/exit-confirmation.md (markdown) -->

# Exit Confirmation — Feature Rationale

Confirmation dialogs are the worst when you actually meant to do the thing. "Do you really really really want to do that?" Nobody enjoys being nag-screened into their own intentional decision.

So why do we have one?

Because we designed the interface for mobile ([ADR-003](../decisions/003-mobile-first-ui-design.md)), and because the tool supports "close to tray," it isn't always obvious when the app is truly exited. We added a dedicated Exit button to make that clear. Then we kept hitting it by accident.

The cost of an accidental exit isn't just an extra click — it's alt-tabbing out of the game (potentially having to revisit desktop shortcuts), relaunching the tool, waiting for it to boot and reconnect, and navigating back to whatever settings page you were trying to click in the first place.

That's annoying enough to justify one nag.

If we find a better home for the Exit button — somewhere we're less likely to mistakenly-finger it — we'll revisit whether the confirmation is still earning its keep.
