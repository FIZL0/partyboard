# N37: soft keyboard without autocorrect for the Code and Relay URL fields

## What changed

- **aurora** gets `aurora::rmlui::InputType::Code`: text input whose SDL properties are
  `SDL_PROP_TEXTINPUT_CAPITALIZATION_NUMBER = SDL_CAPITALIZE_NONE` and
  `SDL_PROP_TEXTINPUT_AUTOCORRECT_BOOLEAN = false` (type stays `SDL_TEXTINPUT_TYPE_TEXT`). `Text` and `Number`
  are unchanged. On Android SDL turns that into `TYPE_CLASS_TEXT | TYPE_TEXT_VARIATION_NORMAL` with no
  `CAP_SENTENCES` / `AUTO_CORRECT` / `AUTO_COMPLETE` flags; on iOS into `UITextAutocapitalizationTypeNone` +
  `UITextAutocorrectionTypeNo`.
- `BaseStringButton::Props` / `StringButton::Props` get `bool code = false`; `focus_input` picks
  `InputType::Code` for it. online.cpp sets `.code = true` on the Join tab's **Code** field and the Players tab's
  **Own Relay URL** field. Nothing else uses it, so every other text field behaves as before.

## Why a patch file and not an aurora commit

The task says "upstream in extern/aurora". The submodule's remote is `encounter/aurora`, and this account has
pull-only rights there (`gh api repos/encounter/aurora --jq .permissions`: `push: false`), and there is no fork.
A commit inside the submodule plus a pointer bump would reference a commit that exists on no remote: every other
worktree, the merge test and any fresh clone would fail `git submodule update`. Creating a fork or opening a PR
upstream is publishing outside this repo, which an unattended run shouldn't do.

So the change is carried as `cmake/patches/aurora/0001-rmlui-code-input-type.patch`, and
`cmake/aurora_patches.cmake` (included just before `add_subdirectory(extern/aurora)`) applies every patch there at
configure time with `git apply`. It skips a patch whose `git apply --reverse --check` passes (already applied, or
merged upstream after a bump), and stops the configure with the `git apply` error if one neither applies nor is
already in. The patch is a configure dependency, so editing it re-runs CMake. The submodule pointer is unchanged;
`extern/aurora` shows as modified content in `git status` after a configure, which is expected.

To upstream it: the patch is a plain `git diff` against aurora 5143394 and can be sent to encounter/aurora as is.
Once it lands and the submodule is bumped, the reverse check skips it and the file (and this note in
aurora_patches.cmake) can be deleted.

## Checks

- Fresh aurora tree (`git -C extern/aurora checkout -- .`), `cmake --preset linux-default-relwithdebinfo`: prints
  "extern/aurora: applied ...", the tree has the two changed files. A second configure prints nothing for the patch
  (reverse check skips it) and doesn't loop.
- `tools/check.sh` passes; `SystemInterface_Aurora.cpp.o` and `string_button.cpp.o` were rebuilt.
- **Android emulator** (AVD hst34 x86_64, `-no-window -gpu host -feature Vulkan`, APK from
  `./gradlew :app:assembleDebug -Ppartyboard.nativeAbis=x86_64`, whose configure ran the same patch step and
  rebuilt aurora's `SystemInterface_Aurora.cpp.o`). Menu → Online, then `adb shell dumpsys input_method` for the
  editor's `inputType`:

  | field | inputType | flags |
  |---|---|---|
  | Join → **Code** | `0x20001` | CLASS_TEXT, MULTI_LINE |
  | Players → **Own Relay URL** (Relay = Own) | `0x20001` | CLASS_TEXT, MULTI_LINE |
  | Players → Player 1 Name (plain Text, baseline) | `0x3c001` | + CAP_SENTENCES 0x4000, AUTO_CORRECT 0x8000, AUTO_COMPLETE 0x10000 |

  The screenshots show the result: `01-code-field.jpg` and `03-relay-url-field.jpg` have a lowercase keyboard with
  shift off, and `02-name-field-baseline.jpg` has shift on (sentence caps). MULTI_LINE was already there:
  `SDL_HINT_RETURN_KEY_HIDES_IME` isn't set, so it isn't from this change.
- The emulator test needed one change that isn't on this branch yet: N36's `auroraNativeSetSurfaceReady` calls in
  `SDLSurface.java`. Without them every Android frame is black. I copied the file from the merge worktree for the
  APK build only and restored it afterwards; it isn't in this commit. Because N36's keyboard-hide fix is missing,
  Enter left the keyboard up, and I used BACK to hide it. Both are N36's to deliver.

## Seen, left alone

- The AOSP keyboard still shows its "Suggest contact names?" strip on the Code field. That strip is LatinIME's
  own prompt, and SDL has no property for it. The only way to remove it would be
  `SDL_PROP_TEXTINPUT_ANDROID_INPUTTYPE_NUMBER` with `TYPE_TEXT_VARIATION_VISIBLE_PASSWORD`, which is
  Android-only and changes the keyboard layout. Not needed: autocorrect and caps, which the task is about, are off.

## Not verified

- **iOS**: there's no Xcode here. SDL's UIKit backend maps the same two properties to
  `autocapitalizationType = None` and `autocorrectionType = No` (and spell checking off), going by the source
  (`SDL_uikitviewcontroller.m`), and it also applies them when text input is already active
  (`UIKit_SetTextInputProperties`). It was not run.
- **A real phone / a third-party keyboard (Gboard, SwiftKey)**: only the emulator's AOSP keyboard was tried. Those
  keyboards honour the EditorInfo flags checked above, but I didn't observe that.
- **Typing a code by tapping keys and joining with it**: the flags and the shift state were checked; I didn't type
  a 4-symbol code through the keyboard and join a lobby with it (that needs a second peer).
- **Desktop**: the Linux build passes, but I didn't open the field there. On X11/Wayland, SDL ignores
  capitalization/autocorrect, and the start path is the same `SDL_StartTextInputWithProperties` that `Number`
  already uses.
- **Upstreaming to encounter/aurora**: not done, because there are no push rights and no fork (see above). Filed as
  N55.
