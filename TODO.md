integrate the command palette to the shortcuts menu. also let it search for latex symbols. the other thing is the preview is blurry somehow.

Good picture. Now I'll do the full swap. The approach: replace GtkPaned editor_paned
with AdwOverlaySplitView editor_split. The split view handles overlay in narrow mode
natively, and since show-sidebar=FALSE hides it even in uncollapsed mode, the toggle
button keeps working. All the position/ratio tracking code disappears.
