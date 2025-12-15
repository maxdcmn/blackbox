# Creating GitHub Release (Quick Steps)

## You've already done:
✅ Tagged: `v1.0.0`  
✅ Pushed tag to GitHub

## Next steps (on GitHub website):

1. **Go to releases page:**
   ```
   https://github.com/maxdcmn/blackbox/releases
   ```

2. **Click "Draft a new release"** (or "Create a new release")

3. **Fill in:**
   - **Choose a tag:** Select `v1.0.0` (should appear in dropdown)
   - **Release title:** `v1.0.0` or `Blackbox Server v1.0.0`
   - **Description:** (optional) Add release notes

4. **Upload the .deb file:**
   - Scroll down to "Attach binaries"
   - Click "selecting them" link
   - Upload: `blackbox-server/blackbox-server_1.0.0.deb`

5. **Click "Publish release"**

## After publishing, the download URL will be:
```
https://github.com/maxdcmn/blackbox/releases/download/v1.0.0/blackbox-server_1.0.0.deb
```

## Test it:
```bash
wget https://github.com/maxdcmn/blackbox/releases/download/v1.0.0/blackbox-server_1.0.0.deb
```

## Quick link to create release:
https://github.com/maxdcmn/blackbox/releases/new?tag=v1.0.0

