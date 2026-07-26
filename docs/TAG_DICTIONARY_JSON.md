# Tag dictionary JSON format

How to write the `.json` file that **Ctrl+I** on the tag overview screen imports
(Phase 55). The import populates the vault's tag **vocabulary** — it registers tag
categories and stores per-tag descriptions. It does not tag any image, video or gallery.

Reach the tag overview with `Shift+T` from the gallery grid, then press `Ctrl+I` and pick
the file.

## Complete example — every field

```json
{
  "tags": [
    {
      "category": "artist",
      "name": "Kaguya",
      "description": "Doujin artist, active 2011-2019"
    },
    {
      "category": "character",
      "name": "Alice",
      "description": "Recurring protagonist"
    },
    {
      "category": "language",
      "name": "japanese",
      "description": ""
    }
  ]
}
```

## Minimal example — required field only

A bare array works just as well as the `{"tags": …}` wrapper, and `name` is the only
field an entry needs:

```json
[
  { "name": "landscape" },
  { "name": "sunset" }
]
```

## What each field means

| Field | Required | Meaning |
|---|---|---|
| `name` | **yes** | The tag itself. Must be non-empty after trimming, and **must not contain a colon** (`:`) — see below. |
| `category` | no | The tag's category. Present → the tag is stored as `category:name` and the category is registered with a colour. Absent or empty → the tag is stored bare, with no category and no colour. |
| `description` | no | Free text shown under the tag on the overview. Absent or empty → **any description the tag already has is left alone**; the import never deletes one. |

Notes:

- **Whitespace around every field is trimmed.** `"  artist  "` and `"artist"` are the
  same category.
- **A colon in `name` is rejected.** The app splits a tag at its *first* colon to find
  the category, so `"name": "artist:Kaguya"` would display split in the wrong place.
  Put the category in the `category` field instead. Such an entry is skipped and
  reported in the summary — it is never silently rewritten.
- **Matching is case-insensitive, first spelling wins.** `Artist` and `artist` are the
  same category; whichever the vault (or the file) saw first is the spelling kept.
  Duplicate entries within one file are skipped.
- **Existing categories keep their colour.** A new category gets the lowest colour no
  category is using yet. Past 16 categories the palette wraps, so colours can repeat.
- **Long text is shortened, not rejected.** A category over 64 bytes or a description
  over 512 bytes is cut at a character boundary (never mid-character) and the entry is
  still imported; the summary says how many fields were shortened.
- **A bad entry never stops the file.** Anything the parser cannot use — a non-object,
  a missing name, a colon in the name, a duplicate — is skipped, counted, and the rest
  of the file still imports. A file that is not valid JSON at all imports nothing.

## After the import

A summary modal reports what changed: categories added, descriptions added, descriptions
updated, and — only when non-zero — entries skipped, categories that could not be
registered, and fields that were shortened. Press any key to dismiss it.

The overview lists tags that something in the vault **carries**. A description imported
for a tag nothing uses yet is stored and kept safe, but only appears on the overview
once that tag is actually applied to something.
