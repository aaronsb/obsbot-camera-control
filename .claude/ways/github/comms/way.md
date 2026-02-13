---
commands: gh\ (pr|issue)
keywords: review pr|pr review|comment|triage|issue response|merge|approve
---
# GitHub Communication Style

## Tone: Finnish Pragmatic

Direct. Technical. No corporate padding. Say what you mean.

### PR Reviews

- Lead with what matters: does it work, is it correct, does it fit
- If something's wrong, say what and why — don't soften it into meaninglessness
- If it's good, say so briefly: "Clean fix, does the right thing" not "Thank you so much for this wonderful contribution to our project!"
- Specific praise > generic praise: "Good call using the SDK path as primary with fallback" beats "Great work!"
- Humor is fine when it lands naturally — don't force it
- Request changes when needed, don't hedge with "maybe consider perhaps..."

### Issue Responses

- Acknowledge the report, confirm or ask for specifics
- Don't promise timelines
- If it's a duplicate, link it and close — no apology needed
- Device reports: ask for `lsusb` output and camera model, don't guess
- Label accurately, triage honestly — "won't fix" is a valid answer when warranted

### Thanking Contributors

Thank people for real things they actually did:
- "Thanks for catching that — the script name was wrong since the rename" (specific)
- "Nice first contribution. The SDK path approach is cleaner than re-scanning." (genuine)

NOT:
- "Thank you for your contribution to this project! We really appreciate your time and effort!" (empty calories)
- "Thanks for opening this issue!" (they reported a bug, not donated a kidney)

### General Principles

- A one-line response is fine when one line is enough
- Technical accuracy over diplomatic vagueness
- "No" is a complete answer when paired with a reason
- Keep commit-style messages in PR merges — what changed and why
- If someone's first PR needs work, be clear about what to fix but don't be a jerk about it — everyone starts somewhere
