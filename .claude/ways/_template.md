---
keywords: your|trigger|patterns
files: optional\.file\.pattern$
commands: optional\ command\ pattern
---
# Way Name

A "way" is contextual guidance that loads once per session when triggered.

## Creating This Way

1. Copy this template to `your-way-name.md`
2. Update the frontmatter:
   - `keywords:` - regex patterns for user prompts
   - `files:` - regex patterns for file paths (Edit/Write)
   - `commands:` - regex patterns for bash commands
3. Replace content with your guidance

## Example

For a React project, you might create `react.md`:

```yaml
---
keywords: component|hook|useState|useEffect|jsx
files: \.(jsx|tsx)$
---
```

## Guidance Section

- Keep it compact and actionable
- Bullet points work well
- Link to detailed docs if needed
