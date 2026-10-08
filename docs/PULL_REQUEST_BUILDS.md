# Pull-request builds

Every pull request to ProsperoRadio is built in full by the
[Build workflow](../.github/workflows/tooling.yml): lint, host tests, the
runtime reproduction, and the same app-folder ZIP a release gets. The result is
an installable copy of the app, kept for 14 days, that a reviewer can put on a
console before merging.

## What a pull request produces

| | Pull request | Tag, or a run started by hand |
| --- | --- | --- |
| Artifact name | `ProsperoRadio-PR<number>-<commit>` | `prospero-radio-<commit>-release` |
| `<commit>` | First seven characters of the pull request's own head commit | The full commit that was built |
| Label file in the app folder | `build-label.txt` holding `PR <number>, <commit>` | None |
| `contentVersion` | Unchanged | Unchanged |

For example, pull request 12 at commit `1a2b3c4...` uploads
`ProsperoRadio-PR12-1a2b3c4`, and its app folder holds a `build-label.txt`
reading `PR 12, 1a2b3c4`.

Two details are deliberate:

- **The commit is the pull request's head**, not `github.sha`. For a pull
  request, `github.sha` is a temporary merge commit that appears nowhere on the
  pull request's page, so an artifact named after it cannot be matched to what
  is being reviewed.
- **The version is not touched.** A test build reports the same
  `contentVersion` as the release it is based on, so the update check and the
  in-app update behave exactly as they will after the merge. The label is a
  separate file.

## Getting the build

1. Open the pull request, then **Checks** and the **Build** run (or the run's
   page under **Actions**).
2. Download the artifact named `ProsperoRadio-PR<number>-<commit>` from the
   run's **Artifacts** list. GitHub requires a signed-in account for this.
3. Unpack it: it holds `PPSA99001.zip` (the app folder, every entry stored as
   0777) and `SHA256SUMS`. There is no image file: the app updates itself in
   place, which needs a folder install. Check the ZIP with
   `sha256sum -c SHA256SUMS`, then install as described in
   [Deployment](DEPLOYMENT.md).

A first-time contributor's pull request does not build until a maintainer
approves the workflow run. That is GitHub's default for public repositories and
is worth keeping: the build runs the pull request's code.

## The label file

`tools/build.sh` writes the environment variable `BUILD_LABEL` as one line to
`build-label.txt` at the root of the app folder, next to `eboot.bin`
(`/app0/build-label.txt` on the console). The workflow sets it for pull
requests only, before the folder is built and archived, so the ZIP contains the
file. A build without it writes no file, so a release never carries one, and an
in-app update replaces the folder and with it the label.

`BUILD_LABEL` must be 1 to 40 characters from letters, digits, spaces and
`, . _ # -`. The build refuses anything else before compiling, so the text is
safe to show as it is.

ProsperoRadio does not display the label yet: today it identifies a build in
the artifact and in the installed folder only. An app screen that shows the
version can read the same file and show nothing when it is missing. Do not put
the label into `param.json` or compare it with anything: it is for people.

The same works on your PC, for a build you want to tell apart on the console:

```bash
BUILD_LABEL="pacing test 2" make
```

## Names and safety

The pull-request name follows the repository's name by itself. The name used
for tags and runs started by hand, `prospero-radio-<commit>-release`, appears twice in the
workflow (the upload, and the release job's download); rename both together. A
tag never takes the pull-request branch, so the release job always finds its
build under that name.

Pull-request runs have a read-only token and no secrets, including for forks.
Do not move this build to `pull_request_target` to post links or comments: that
event runs with write access and secrets, and building a contributor's code
under it hands both to that code.
