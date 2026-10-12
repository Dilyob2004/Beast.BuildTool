# Beast default icon

`Default.ico`: nine PNG-backed RGBA icon sizes, 16/20/24/32/40/48/64/128/256 px.
`Default.png`: original generated artwork, preserved with its alpha channel.
Created with built-in imagegen; no Unreal artwork is used in the new asset.

Prompt:
> Create a minimal Windows application icon for Beast: only a single uppercase Latin letter "B", bold clean sans-serif, white letter on a dark charcoal rounded-square tile, transparent outside the tile. Flat solid colors. Large centered letter with balanced margins, readable at 16px. No illustration, no animal, no claw shapes, no embellishments, no gradient, no shadows, no extra text. One square icon, not a mockup.

Regenerate the ICO from the repository root with PowerShell:

```powershell
.\BuildTool\Tools\Convert-Icon.ps1 -Source BuildTool/Templates/Windows/Resources/Default.png -Destination BuildTool/Templates/Windows/Resources/Default.ico
```

New projects copy this template. Existing project icons are deliberately not
overwritten by generation; replace their `Platforms/Windows/Resources/Default.ico`
explicitly if wanted. EcoSbor's unchanged original default was replaced together
with this template. Its Windows resources need a rebuild to update the EXE.
