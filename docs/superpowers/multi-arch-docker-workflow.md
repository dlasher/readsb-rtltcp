# Multi-Arch Docker Build & Publish Workflow

A reusable GitHub Actions workflow pattern for building and publishing multi-architecture Docker images (linux/amd64 + linux/arm64) to GitHub Container Registry (GHCR). Produces a single manifest tag that Docker clients resolve to the correct architecture automatically.

## Prerequisites

- GitHub repository with Actions enabled
- `Dockerfile` at repository root
- `.dockerignore` — exclude `.git/`, `*.o`, build artifacts, test data
- GHCR visibility: after first push, make the package public via Package Settings on GitHub (or the image stays private)

## Workflow Template

Create `.github/workflows/docker-publish.yml`:

```yaml
name: Build and Publish Docker Image

on:
  push:
    branches: [main]
    tags: ['v*']

env:
  REGISTRY: ghcr.io
  # CHANGE: image name (ghcr.io/{owner}/{image-name})
  IMAGE_NAME: owner/image-name

jobs:
  build:
    runs-on: ubuntu-latest
    permissions:
      contents: read
      packages: write

    steps:
      - uses: actions/checkout@v4

      - name: Set up QEMU
        uses: docker/setup-qemu-action@v3

      - name: Set up Docker Buildx
        uses: docker/setup-buildx-action@v3

      - name: Docker metadata
        id: meta
        uses: docker/metadata-action@v5
        with:
          images: ${{ env.REGISTRY }}/${{ env.IMAGE_NAME }}
          tags: |
            type=raw,value=latest,enable=${{ github.ref == 'refs/heads/main' }}
            type=semver,pattern={{version}}
            type=semver,pattern={{major}}.{{minor}}

      - name: Log in to GHCR
        uses: docker/login-action@v3
        with:
          registry: ${{ env.REGISTRY }}
          username: ${{ github.actor }}
          password: ${{ secrets.GITHUB_TOKEN }}

      - name: Build and push
        uses: docker/build-push-action@v6
        with:
          context: .
          # CHANGE: add/remove platforms as needed
          platforms: linux/amd64,linux/arm64
          push: true
          tags: ${{ steps.meta.outputs.tags }}
          labels: ${{ steps.meta.outputs.labels }}
          cache-from: type=gha
          cache-to: type=gha,mode=max
```

## Adaptation Guide

For each new project, customize these four things:

### 1. Image Name

Set `IMAGE_NAME` to `owner/image-name` matching the GHCR namespace:
- GitHub user: `username/repo-name` → `ghcr.io/username/repo-name`
- GitHub org: `org-name/repo-name` → `ghcr.io/org-name/repo-name`

### 2. Platforms

The `platforms` line controls which architectures are built. Common values:

```yaml
platforms: linux/amd64,linux/arm64          # x86_64 + ARM64 (Pi 4/5)
platforms: linux/amd64,linux/arm64,linux/arm/v7  # also ARM32 (Pi 3)
platforms: linux/amd64                       # x86_64 only
```

Each platform compiles natively under QEMU emulation. Adding ARM32 (`arm/v7`) roughly doubles build time. ARM64 roughly triples it versus single-arch.

### 3. Triggers

The `on.push` section controls when the workflow runs:

```yaml
on:
  push:
    branches: [main]         # always tag :latest
    tags: ['v*']             # tag with semver (v1.0 → 1, 1.0)
```

To skip `:latest` on branch pushes and only publish on tags:

```yaml
on:
  push:
    tags: ['v*']
```

### 4. .dockerignore

Must exist at repository root. Minimizes build context size and prevents stale `.o` files from skipping compilation:

```
.git/
.gitignore
.dockerignore
*.o
*.swp
docs/
package-*/
```

## How It Works

| Action | Role |
|--------|------|
| `setup-qemu-action` | Installs QEMU static binaries for cross-architecture emulation. Enables Docker to run ARM64 binaries on the x86_64 runner during the build step. |
| `setup-buildx-action` | Creates a Buildx builder instance capable of producing multi-architecture images. Without this, `docker build` only produces the native platform. |
| `metadata-action` | Generates tag lists from git refs. On branch push: `latest`. On tag push: semver tags (`1`, `1.0`, `1.0.0`) + `latest`. |
| `build-push-action` | Builds all specified platforms in parallel (QEMU-emulated), pushes each to GHCR, and creates an OCI image index (manifest list) pointing to all of them. |
| `cache-from/to: type=gha` | Saves and restores build cache in GitHub Actions storage. Subsequent runs skip unchanged layers, reducing build time by ~70%. |

On arm64 machines (Pi 4/5, Mac M-series), `docker pull ghcr.io/owner/image:latest` automatically selects the `linux/arm64` layer. On x86_64, it selects `linux/amd64`.

## Verification

After a successful workflow run, verify both architectures are in the manifest:

```bash
docker manifest inspect ghcr.io/owner/image-name:latest
```

Expected output (simplified):

```json
{
   "mediaType": "application/vnd.oci.image.index.v1+json",
   "manifests": [
      { "platform": { "architecture": "amd64", "os": "linux" } },
      { "platform": { "architecture": "arm64", "os": "linux" } }
   ]
}
```

If you only see one platform, the build may have fallen back to single-arch (check `setup-buildx-action` is present).

## Troubleshooting

### Build takes >30 minutes

QEMU-emulated C++ compilation is slow. Expected times for this workflow:
- `linux/amd64` only: ~2 min
- Both amd64 + arm64: ~15-20 min
- Adding arm/v7: ~25-35 min

Mitigations:
- `cache-from/to: type=gha` — caches layers across runs
- Build more frequently (smaller incremental diffs)
- Consider native ARM64 runners (`ubuntu-24.04-arm`) for faster arm64 builds

### "invalid token" / "DENIED" when pulling

GHCR packages default to private. Make it public after first push:
1. Go to `https://github.com/settings/packages`
2. Find the package
3. Package Settings → Change visibility to Public

### QEMU errors during build

Usually a transient issue with the `setup-qemu-action@v3`. Re-run the workflow. If persistent, pin a specific QEMU version:

```yaml
- uses: docker/setup-qemu-action@v3
  with:
    image: tonistiigi/binfmt:latest
```
