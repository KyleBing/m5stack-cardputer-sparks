# Version

Main menu key: `v`

Shows firmware version and author.

## Screenshots

<div class="shot-row">

![version-main](/shots/app_version_v1.10.png)

</div>

This documentation corresponds to:

| Field | Value |
|-------|-------|
| Version | **{{APP_VERSION}}** |
| Updated | {{APP_UPDATE_TIME}} |
| Author | {{APP_AUTHOR}} |

## Shortcuts

| Key | Action |
|-----|--------|
| `r` / `R` | Refresh fireworks animation |

## Usage

Version strings live in `include/app_version.h` (`APP_VERSION` / `APP_UPDATE_TIME`). Change only that header when releasing; the M5Burner packaging script reads the same source.
