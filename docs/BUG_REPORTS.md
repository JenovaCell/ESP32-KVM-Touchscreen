# Reporting bugs

Bugs are tracked in the **KVM** Jira project (https://smollplates.atlassian.net, board "KVM board").
To get one fixed, create a Bug there and give the issue key (for example `KVM-12`) to Claude.

## What a useful bug has

Paste this into the description and fill it in:

```
## Versions
Firmware version (boot screen):    e.g. v0.8.0
Mac app version (menu bar menu):   e.g. v0.8.0
Last version that worked (if known):

## Where
Target when it happened:           MAC / WORK / GAME
Auto-switch by app on or off:      (if it is about switching)

## Steps to Reproduce
1.
2.
3.

## Expected result

## Actual result
(what the board screen and the Mac menu said, word for word)

## How often
Every time / sometimes (about N in 10) / once

## Evidence (optional but very helpful)
Photo of the board screen (bottom lines included), and the text from the Mac menu
"Copy diagnostics" taken right after the problem.

## Severity
Blocks use / annoying / cosmetic
```

Version numbers matter most: they tell us which build to look at. "Steps to Reproduce" is second:
without it a bug is a guess. The same template is set up as the default description of Bug tickets
in Jira (see below).

## What happens next

1. Claude reads the ticket, reproduces it from the code, and fixes it.
2. The fix goes out as a new version: `VERSION` is bumped and `CHANGELOG.md` gets an entry
   that names the ticket key, e.g. `Fixed: ... (KVM-12)`.
3. Claude comments on the ticket with the version and the direct download link, and moves it
   to the next status. You test it on the hardware and close it or reopen it.

Claude only acts on tickets you point it to; it does not watch the board in the background.
