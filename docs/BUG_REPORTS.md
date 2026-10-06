# Reporting bugs

Bugs are tracked in the **KVM** Jira project (https://smollplates.atlassian.net, board "KVM board").
To get one fixed, create a Bug there and give the issue key (for example `KVM-12`) to Claude.

## What a useful bug has

Paste this into the description and fill it in:

```
Firmware version (boot screen):   e.g. v0.4.1 b15
Mac app version (menu bar menu):  e.g. v0.4.1
Device firmware shown in menu:    e.g. v0.4.1 b15

Target when it happened:          MAC / WORK / GAME
What I did (steps):
1.
2.
3.

Expected:
Actual (what the board screen and the Mac menu said, word for word):

Photo or serial output (optional):
```

Version numbers matter most: they tell us which build to look at.

## What happens next

1. Claude reads the ticket, reproduces it from the code, and fixes it.
2. The fix goes out as a new version: `VERSION` is bumped and `CHANGELOG.md` gets an entry
   that names the ticket key, e.g. `Fixed: ... (KVM-12)`.
3. Claude comments on the ticket with the version and the direct download link, and moves it
   to the next status. You test it on the hardware and close it or reopen it.

Claude only acts on tickets you point it to; it does not watch the board in the background.
