# Nexus File Manager — implementation status

## Implemented in this increment

The Files application remains a bounded kernel-shell view over the existing VFS. It does not run a separate userspace file-manager process.

- **Navigation:** Existing nested-directory navigation is retained. The VFS now accepts absolute directory changes transactionally; an invalid path restores the previous working directory.
- **Tabs:** Up to four volatile tabs retain independent directory paths, search queries, type filters, selected rows and scroll positions. Ctrl+T opens a tab, Ctrl+W closes it, Ctrl+Tab cycles forward and Ctrl+Shift+Tab cycles backward.
- **Search and filtering:** Current-directory names can be searched with case-insensitive substring matching. F4 cycles between all items, files only and directories only.
- **Sorting:** Name, type and size sorting uses a stable bottom-up merge sort with O(n log n) comparison complexity.
- **File operations:** Existing new-file, new-folder, edit, rename, same-directory copy/move, run-ELF and delete operations remain. Ctrl+C, Ctrl+X and Ctrl+V now provide file copy/move between tabs on writable NexusFS storage; copy creates a unique `_COPY`/numbered destination name, while move preserves the source filename and refuses collisions.
- **Properties:** F3 toggles selected-item details including name, detected extension class, byte size and the VFS writable/read-only result.
- **Storage summary:** The list shows the number of cached entries and the sum of visible regular-file sizes in the current directory. This is not a filesystem free-space report.
- **Directory capacity:** The Files metadata cache and FAT32 directory-entry cache are increased to 512 items. FAT32 marks a listing as truncated when the cache fills; the UI reports the bounded listing limit.
- **Large-file data movement:** File clipboard transfers use the existing NXFS block-copy implementation, which copies one device block at a time and validates the source checksum before publishing the destination. The editor remains limited to its existing bounded in-memory edit buffer.
- **Refresh responsiveness:** Directory refresh now reads directory metadata only. It no longer opens a file to sample preview bytes or writes a serial log record for every row. A selected file's text preview is read only on explicit open/preview input.

## Keyboard controls

| Key | Action |
|---|---|
| Enter | Open folder or preview/edit supported small file |
| Backspace | Parent directory |
| Ctrl+F | Search current directory |
| S | Cycle name/type/size sorting |
| F3 | Toggle selected-item properties |
| F4 | Cycle All / Files / Folders filter |
| Ctrl+T | New tab at current path |
| Ctrl+W | Close current tab |
| Ctrl+Tab / Ctrl+Shift+Tab | Switch tabs |
| Ctrl+C / Ctrl+V | Copy and paste selected supported NXFS file |
| Ctrl+X / Ctrl+V | Move selected supported NXFS file |
| R / C / M / D / Delete | Existing rename / copy-prompt / move-prompt / new-folder / delete actions |

## Explicit remaining work — not implemented

| Requested area | Current limitation | Needed before claiming support |
|---|---|---|
| Image thumbnails and rich previews | File extensions are classified as image/archive, but there is no PNG/JPEG/WebP decoder, thumbnail cache or image-preview renderer | Add safe decoders, bounded async thumbnail generation, cache invalidation and image preview tests |
| Drag and drop | Files cannot be dragged between list items, tabs or windows | Add pointer capture, drag payloads, target validation, cancel/drop feedback and cross-tab integration tests |
| Archive handling | ZIP/TAR/GZIP/7z extensions are identified only; there is no archive listing, extraction or creation | Add validated archive readers/writers, decompression limits, path traversal defenses and integrity tests |
| External-drive management | The current scanner mounts the first supported volume it finds; there is no multi-volume picker, eject workflow or independently mounted volume namespace | Add mount registry, stable volume identities, safe unmount/eject and UI selection |
| Permissions | UI can report a backend read-only result, but the VFS has no per-user ownership, POSIX modes or ACL enforcement | Add identities, ownership/mode metadata and checks at every filesystem/process boundary |
| Network locations | No SMB, NFS, WebDAV or other network filesystem mount client is integrated into the VFS | Add an authenticated network filesystem provider and timeout/offline handling |
| Disk capacity/free space | The UI reports current-directory file bytes only; it does not report free/total filesystem space | Add filesystem capacity/free-space queries for each mounted volume |
| Recursive search | Search covers only the current directory; it does not build a recursive index | Add a cancellable incremental traversal/index service |
| Directory scale and asynchronous I/O | List display and sorting are bounded at 512 entries and refresh is metadata-only, but FAT32 mount/directory-chain reads and storage operations still use synchronous device I/O. NXFS itself has a 64-entry metadata table | Add asynchronous block I/O, incremental directory iterators, cancellation and larger on-disk metadata structures |
| Persistent tabs | Tab paths and view state are volatile across reboot | Add versioned user preferences and restore validation |

## Bounded behavior and safety

- File-list sort/filter arrays are sized to the declared 512-entry ceiling; malformed names and invalid sort/filter modes are rejected.
- FAT32 parsing keeps a fixed metadata cache and reports truncation instead of overrunning it.
- The NXFS absolute path transfer API validates both paths and only operates on mounted writable NXFS. It does not bypass FAT32 read-only policy.
- File delete still refuses non-empty directories and read-only targets.
- The text editor's 4 KiB-scale buffer is not used for bulk file copying.
- A 512-entry cache does not mean the filesystem can store only 512 files: the current NXFS on-disk metadata table is separately limited to 64 entries, while FAT32 directory enumeration has its own bounded cluster-chain limit.

This is a meaningful file-manager upgrade, not yet a fully featured desktop file manager. The missing components above are intentionally not presented as working features.
