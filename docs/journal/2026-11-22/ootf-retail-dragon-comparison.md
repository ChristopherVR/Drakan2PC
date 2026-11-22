# Retail OOTF dragon comparison

The supplied disc build reports 365/435. The publisher’s 375/445 build was extracted into a separate private folder
for a Winsock comparison. The disc and patched data agree on all three red launch-pad positions and headings, the
Green Dragon’s health and flight speeds, and the default poison breath’s damage, reload, energy and projectile values.
The poison class detects objects and has zero time during which it ignores ground and building collisions.

The port pursued players and dealt 40 damage in two-client Rise to Power and Island of Death runs. The Rise to Power
server recorded three
consecutive attacks at 26.808, 27.329 and 27.851 seconds. It applies the damage in the same update as the breath event.
That identifies an observable timing and collision question for the retail comparison.

The unmodified 445 dedicated server started Island of Death but faulted as the first client joined on Windows and
Wine. The Wine listen host on Island of Death and the Wine dedicated host on Rise to Power hit the same fault in
the original game module. The unpatched disc build reached the Master of the Dragon menu under Wine and reported
session creation failure when starting Island of Death. The original Windows run reached the DirectPlay and Windows
Firewall prompts. The live retail pursuit, aiming and impact comparison remains pending.
