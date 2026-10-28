// Credits: TwilitRealm

#include "settings.hpp"

#include "aurora/gfx.h"
#include "bool_button.hpp"
#include "button_glyphs.hpp"
#include "controller_config.hpp"
#include "port/audio.h"
#include "port/config.hpp"
#include "port/log_file.hpp"
#include "port/button_icons.h"
#include "../imgui/ImGuiEngine.hpp"
#include "../file_select.hpp"
#include "graphics_tuner.hpp"
#include "menu_bar.hpp"
#include "number_button.hpp"
#include "pane.hpp"
#include "prelaunch.hpp"
#include "ui.hpp"

#include <algorithm>
#include <fmt/format.h>
#include <aurora/aurora.h>
#include <game/disp.h>
#include <gx/GXAurora.h>
#include <vi.h>

namespace partyboard::ui {
namespace {

    constexpr std::array kLanguageNames = {
        "English",
        "German",
        "French",
        "Spanish",
        "Italian",
    };

    constexpr std::array kCardFileTypes = {
        "Card Image",
        "GCI Folder",
    };

    // N49: indexed by PB_ICONS_* (port/button_icons.h)
    constexpr std::array kButtonIconNames = {
        "Auto",
        "PlayStation",
        "Xbox",
        "Switch",
        "Generic",
        "GameCube",
    };

    // N130: the Safe Area setting's value as shown (0 Auto, 1..10 a margin in %, -1 Off)
    Rml::String safe_area_name(int value)
    {
        if (value < 0) {
            return "Off";
        }
        if (value == 0) {
            return "Auto";
        }
        return fmt::format("{} %", value);
    }

    constexpr std::array kFpsOverlayCornerNames = {
        "Top Left",
        "Top Right",
        "Bottom Left",
        "Bottom Right",
    };

    constexpr std::array kGyroInputModeLabels = {
        "Sensor",
        "Mouse",
    };

    bool try_parse_backend(std::string_view backend, AuroraBackend &outBackend)
    {
        if (backend == "auto") {
            outBackend = BACKEND_AUTO;
            return true;
        }
        if (backend == "d3d11") {
            outBackend = BACKEND_D3D11;
            return true;
        }
        if (backend == "d3d12") {
            outBackend = BACKEND_D3D12;
            return true;
        }
        if (backend == "metal") {
            outBackend = BACKEND_METAL;
            return true;
        }
        if (backend == "vulkan") {
            outBackend = BACKEND_VULKAN;
            return true;
        }
        if (backend == "opengl") {
            outBackend = BACKEND_OPENGL;
            return true;
        }
        if (backend == "opengles") {
            outBackend = BACKEND_OPENGLES;
            return true;
        }
        if (backend == "webgpu") {
            outBackend = BACKEND_WEBGPU;
            return true;
        }
        if (backend == "null") {
            outBackend = BACKEND_NULL;
            return true;
        }

        return false;
    }

    std::string_view backend_name(AuroraBackend backend)
    {
        switch (backend) {
            default:
                return "Auto";
            case BACKEND_D3D12:
                return "D3D12";
            case BACKEND_D3D11:
                return "D3D11";
            case BACKEND_METAL:
                return "Metal";
            case BACKEND_VULKAN:
                return "Vulkan";
            case BACKEND_OPENGL:
                return "OpenGL";
            case BACKEND_OPENGLES:
                return "OpenGL ES";
            case BACKEND_WEBGPU:
                return "WebGPU";
            case BACKEND_NULL:
                return "Null";
        }
    }

    std::string_view backend_id(AuroraBackend backend)
    {
        switch (backend) {
            default:
                return "auto";
            case BACKEND_D3D12:
                return "d3d12";
            case BACKEND_D3D11:
                return "d3d11";
            case BACKEND_METAL:
                return "metal";
            case BACKEND_VULKAN:
                return "vulkan";
            case BACKEND_OPENGL:
                return "opengl";
            case BACKEND_OPENGLES:
                return "opengles";
            case BACKEND_WEBGPU:
                return "webgpu";
            case BACKEND_NULL:
                return "null";
        }
    }

    std::vector<AuroraBackend> available_backends()
    {
        std::vector<AuroraBackend> backends;
        backends.emplace_back(BACKEND_AUTO);
        size_t backendCount = 0;
        const AuroraBackend *raw = aurora_get_available_backends(&backendCount);
        for (size_t i = 0; i < backendCount; ++i) {
            // Do not expose NULL or D3D11
            if (raw[i] != BACKEND_NULL && raw[i] != BACKEND_D3D11) {
                backends.emplace_back(raw[i]);
            }
        }
        return backends;
    }

    AuroraBackend configured_backend()
    {
        AuroraBackend configuredBackend = BACKEND_AUTO;
        const auto configuredId = getSettings().backend.graphicsBackend.getValue();
        if (!try_parse_backend(configuredId, configuredBackend)) {
            configuredBackend = BACKEND_AUTO;
        }
        return configuredBackend;
    }

    // TODO PC
    void reset_for_speedrun_mode()
    {
        getSettings().game.infiniteHearts.setValue(false);

        getSettings().game.enableTurboKeybind.setValue(false);
    }

    const Rml::String kInternalResolutionHelpText = "Configure the resolution used for rendering the game. Higher values are more demanding on "
                                                    "your graphics hardware.";
    const Rml::String kShadowResolutionHelpText = "Configure the shadow-map resolution. Higher values improve shadow quality but increase GPU "
                                                  "and memory usage.";
    const Rml::String kBloomHelpText = "Configure the post-processing bloom effect. Classic uses the original bloom pass; Dusk uses "
                                       "a higher-quality bloom pass.";
    const Rml::String kBloomBrightnessHelpText = "Configure bloom intensity. Higher values make bright areas glow more strongly.";
    const Rml::String kUnlockFramerateHelpText = "Uses inter-frame interpolation to enable higher frame rates.<br/><br/>May introduce minor "
                                                 "visual artifacts or animation glitches.";

    int float_setting_percent(ConfigVar<float> &var)
    {
        return static_cast<int>(var.getValue() * 100.0f + 0.5f);
    }

    struct ConfigBoolProps {
        Rml::String key;
        Rml::String icon;
        Rml::String helpText;
        std::function<void(bool)> onChange;
        std::function<bool()> isDisabled;
    };

    SelectButton &config_bool_select(Pane &leftPane, Pane &rightPane, ConfigVar<bool> &var, ConfigBoolProps props)
    {
        auto &button = leftPane.add_child<BoolButton>(BoolButton::Props {
            .key = std::move(props.key),
            .icon = std::move(props.icon),
            .getValue = [&var] { return var.getValue(); },
            .setValue =
                [&var, callback = std::move(props.onChange)](bool value) {
                    if (value == var.getValue()) {
                        return;
                    }
                    var.setValue(value);
                    config::Save();
                    if (callback) {
                        callback(value);
                    }
                },
            .isDisabled = std::move(props.isDisabled),
            .isModified = [&var] { return var.getValue() != var.getDefaultValue(); },
        });
        leftPane.register_control(button, rightPane, [helpText = std::move(props.helpText)](Pane &pane) {
            pane.clear();
            pane.add_rml(helpText);
        });
        return button;
    }

    SelectButton &config_percent_select(Pane &leftPane, Pane &rightPane, ConfigVar<float> &var, Rml::String key, Rml::String helpText, int min,
        int max, int step = 5, std::function<bool()> isDisabled = {})
    {
        auto &button = leftPane.add_child<NumberButton>(NumberButton::Props {
            .key = std::move(key),
            .getValue = [&var] { return float_setting_percent(var); },
            .setValue =
                [&var, min, max](int value) {
                    var.setValue(std::clamp(value, min, max) / 100.0f);
                    config::Save();
                },
            .isDisabled = std::move(isDisabled),
            .isModified = [&var] { return var.getValue() != var.getDefaultValue(); },
            .min = min,
            .max = max,
            .step = step,
            .suffix = "%",
        });
        leftPane.register_control(button, rightPane, [helpText = std::move(helpText)](Pane &pane) {
            pane.clear();
            pane.add_text(helpText);
        });
        return button;
    }

    template <typename T>
    void graphics_tuner_control(Window &window, Pane &leftPane, Pane &rightPane, ConfigVar<T> &var, const GraphicsTunerProps &props, bool prelaunch)
    {
        leftPane.register_control(leftPane
                                      .add_select_button({
                                          .key = props.title,
                                          .getValue =
                                              [&var, option = props.option] {
                                                  if constexpr (std::is_same_v<T, float>) {
                                                      return format_graphics_setting_value(option, float_setting_percent(var));
                                                  }
                                                  else {
                                                      return format_graphics_setting_value(option, static_cast<int>(var.getValue()));
                                                  }
                                              },
                                          .isModified = [&var] { return var.getValue() != var.getDefaultValue(); },
                                          .submit = false,
                                      })
                                      .on_nav_command([&window, props, prelaunch](Rml::Event &, NavCommand cmd) {
                                          if (cmd == NavCommand::Confirm || cmd == NavCommand::Left || cmd == NavCommand::Right) {
                                              window.push(std::make_unique<GraphicsTuner>(props, prelaunch));
                                              return true;
                                          }
                                          return false;
                                      }),
            rightPane, [helpText = props.helpText](Pane &pane) {
                pane.clear();
                pane.add_text(helpText);
            });
    }

} // namespace

SettingsWindow::SettingsWindow(bool prelaunch)
    : mPrelaunch(prelaunch)
{
    if (prelaunch) {
        mSuppressNavFallback = true;
        add_tab("Prelaunch", [this](Rml::Element *content) {
            auto &leftPane = add_child<Pane>(content, Pane::Type::Controlled);
            auto &rightPane = add_child<Pane>(content, Pane::Type::Uncontrolled);

            leftPane.register_control(leftPane
                                          .add_select_button({
                                              .key = "Disc Image",
                                              .getValue =
                                                  [] {
                                                      const auto &path = prelaunch_state().configuredDiscPath;
                                                      std::string display;
                                                      if (path.empty()) {
                                                          display = "(none)";
                                                      }
                                                      else {
                                                          display = display_name_for_path(path);
                                                          if (display.empty()) {
                                                              display = path;
                                                          }
                                                      }
                                                      return display;
                                                  },
                                              .isModified =
                                                  [] {
                                                      const auto &state = prelaunch_state();
                                                      const auto &active = state.activeDiscPath;
                                                      return !active.empty() && state.configuredDiscPath != active;
                                                  },
                                          })
                                          .on_pressed([] { open_iso_picker(); }),
                rightPane, [](Pane &pane) {
                    pane.add_rml("Set the disc image that Party Board uses to launch the game.<br/><br/>"
                                 "Changes require a restart.");
                });
            leftPane.register_control(leftPane.add_select_button({
                                          .key = "Language",
                                          .getValue =
                                              [] {
                                                  const auto &state = prelaunch_state();
                                                  if (!state.configuredDiscCanLaunch || !state.configuredDiscInfo.isPal) {
                                                      return kLanguageNames[0];
                                                  }
                                                  const u8 idx = static_cast<u8>(getSettings().game.language.getValue());
                                                  return kLanguageNames[idx];
                                              },
                                          .isDisabled =
                                              [] {
                                                  const auto &state = prelaunch_state();
                                                  return !state.configuredDiscCanLaunch || !state.configuredDiscInfo.isPal;
                                              },
                                          .isModified = [] { return getSettings().game.language.getValue() != prelaunch_state().initialLanguage; },
                                      }),
                rightPane, [](Pane &pane) {
                    for (int i = 0; i < kLanguageNames.size(); i++) {
                        pane.add_button({
                                            .text = kLanguageNames[i],
                                            .isSelected = [i] { return getSettings().game.language.getValue() == static_cast<GameLanguage>(i); },
                                        })
                            .on_pressed([i] {
                                // mDoAud_seStartMenu(kSoundItemChange); // TODO PC
                                getSettings().game.language.setValue(static_cast<GameLanguage>(i));
                                config::Save();
                            });
                    }
                    pane.add_rml("<br/>Changes require a restart.");
                });
            leftPane.register_control(
                leftPane.add_select_button({
                    .key = "Graphics Backend",
                    .getValue = [] { return Rml::String { backend_name(configured_backend()) }; },
                    .isModified = [] { return getSettings().backend.graphicsBackend.getValue() != prelaunch_state().initialGraphicsBackend; },
                }),
                rightPane, [](Pane &pane) {
                    const auto availableBackends = available_backends();
                    for (const auto backend : availableBackends) {
                        pane.add_button({
                                            .text = Rml::String { backend_name(backend) },
                                            .isSelected = [backend] { return configured_backend() == backend; },
                                        })
                            .on_pressed([backend] {
                                // mDoAud_seStartMenu(kSoundItemChange); // TODO PC
                                getSettings().backend.graphicsBackend.setValue(std::string { backend_id(backend) });
                                config::Save();
                            });
                    }
                    pane.add_rml("<br/>Changes require a restart.");
                });
            leftPane.register_control(
                leftPane.add_select_button({
                    .key = "Save File Type",
                    .getValue = [] { return kCardFileTypes[getSettings().backend.cardFileType.getValue()]; },
                    .isModified = [] { return getSettings().backend.cardFileType.getValue() != prelaunch_state().initialCardFileType; },
                }),
                rightPane, [](Pane &pane) {
                    for (int i = 0; i < kCardFileTypes.size(); i++) {
                        pane.add_button({
                                            .text = kCardFileTypes[i],
                                            .isSelected = [i] { return getSettings().backend.cardFileType.getValue() == i; },
                                        })
                            .on_pressed([i] {
                                // mDoAud_seStartMenu(kSoundItemChange); // TODO PC
                                getSettings().backend.cardFileType.setValue(i);
                                config::Save();
                            });
                    }
                });
        });
    }

    add_tab("Video", [this](Rml::Element *content) {
        auto &leftPane = add_child<Pane>(content, Pane::Type::Controlled);
        auto &rightPane = add_child<Pane>(content, Pane::Type::Uncontrolled);

        leftPane.add_section("Display");

        leftPane.register_control(leftPane.add_button("Toggle Fullscreen").on_pressed([] {
            // mDoAud_seStartMenu(kSoundItemChange); // TODO PC
            getSettings().video.enableFullscreen.setValue(!getSettings().video.enableFullscreen);
            VISetWindowFullscreen(getSettings().video.enableFullscreen);
            config::Save();
        }),
            rightPane, [](Pane &pane) { pane.clear(); });
        leftPane.register_control(leftPane.add_button("Restore Default Window Size").on_pressed([] {
            // mDoAud_seStartMenu(kSoundItemChange); // TODO PC
            getSettings().video.enableFullscreen.setValue(false);
            VISetWindowFullscreen(false);
            VISetWindowSize(HU_FB_WIDTH * 2, HU_FB_HEIGHT * 2);
            VICenterWindow();
        }),
            rightPane, [](Pane &pane) { pane.clear(); });
        config_bool_select(leftPane, rightPane, getSettings().video.enableVsync,
            {
                .key = "Enable VSync",
                .helpText = "Synchronizes the frame rate to your monitor's refresh rate.",
                .onChange = [](bool value) { aurora_enable_vsync(value); },
            });
        config_bool_select(leftPane, rightPane, getSettings().video.lockAspectRatio,
            {
                .key = "Lock 4:3 Aspect Ratio",
                .helpText = "Lock the game's aspect ratio to the original.",
                .onChange = [](bool value) { AuroraSetViewportPolicy(value ? AURORA_VIEWPORT_FIT : AURORA_VIEWPORT_STRETCH); },
            });
        // N130: Safe Area: Auto, a margin of 1-10 %, Off
        leftPane.register_control(leftPane.add_select_button({
                                      .key = "Safe Area",
                                      .getValue = [] { return safe_area_name(getSettings().video.safeArea.getValue()); },
                                      .isModified =
                                          [] {
                                              const auto &safe = getSettings().video.safeArea;
                                              return safe.getValue() != safe.getDefaultValue();
                                          },
                                  }),
            rightPane, [](Pane &pane) {
                for (int i = 0; i <= 11; ++i) {
                    const int value = i <= 10 ? i : -1;
                    pane.add_button({
                                        .text = safe_area_name(value),
                                        .isSelected = [value] { return getSettings().video.safeArea.getValue() == value; },
                                    })
                        .on_pressed([value] {
                            getSettings().video.safeArea.setValue(value);
                            config::Save();
                        });
                }
                pane.add_rml("<br/>Keeps the picture, its HUD and the menus inside the screen's safe area. Auto keeps "
                             "clear of a phone's notch, camera cutout and home bar; a margin keeps that much of the "
                             "screen clear on every side as well (for a TV that crops its edges). Off uses the whole "
                             "screen.");
            });
        config_bool_select(leftPane, rightPane, getSettings().game.pauseOnFocusLost,
            {
                .key = "Pause on Focus Lost",
                .isDisabled = [] { return IsMobile; },
            });
        leftPane.register_control(leftPane.add_select_button({
                                      .key = "Show FPS Counter",
                                      .getValue =
                                          [] {
                                              if (!getSettings().video.enableFpsOverlay.getValue()) {
                                                  return Rml::String { "Off" };
                                              }
                                              const int idx = getSettings().video.fpsOverlayCorner.getValue();
                                              return Rml::String { kFpsOverlayCornerNames[idx] };
                                          },
                                      .isModified =
                                          [] {
                                              const auto &enable = getSettings().video.enableFpsOverlay;
                                              const auto &corner = getSettings().video.fpsOverlayCorner;
                                              return enable.getValue() != enable.getDefaultValue()
                                                  || (enable.getValue() && corner.getValue() != corner.getDefaultValue());
                                          },
                                  }),
            rightPane, [](Pane &pane) {
                pane.add_button({
                                    .text = "Off",
                                    .isSelected = [] { return !getSettings().video.enableFpsOverlay.getValue(); },
                                })
                    .on_pressed([] {
                        // mDoAud_seStartMenu(kSoundItemChange); // TODO PC
                        getSettings().video.enableFpsOverlay.setValue(false);
                        config::Save();
                    });
                for (int i = 0; i < static_cast<int>(kFpsOverlayCornerNames.size()); ++i) {
                    pane
                        .add_button({
                            .text = kFpsOverlayCornerNames[i],
                            .isSelected
                            = [i] { return getSettings().video.enableFpsOverlay.getValue() && getSettings().video.fpsOverlayCorner.getValue() == i; },
                        })
                        .on_pressed([i] {
                            // mDoAud_seStartMenu(kSoundItemChange); // TODO PC
                            getSettings().video.enableFpsOverlay.setValue(true);
                            getSettings().video.fpsOverlayCorner.setValue(i);
                            config::Save();
                        });
                }
                pane.add_rml("<br/>Display the current framerate in a corner of the screen while playing.");
            });

        leftPane.add_section("Resolution");
        graphics_tuner_control(*this, leftPane, rightPane, getSettings().game.internalResolutionScale,
            GraphicsTunerProps {
                .option = GraphicsOption::InternalResolution,
                .title = "Internal Resolution",
                .helpText = kInternalResolutionHelpText,
                .valueMin = 0,
                .valueMax = 12,
                .defaultValue = 0,
            },
            mPrelaunch);
        graphics_tuner_control(*this, leftPane, rightPane, getSettings().game.shadowResolutionMultiplier,
            GraphicsTunerProps {
                .option = GraphicsOption::ShadowResolution,
                .title = "Shadow Resolution",
                .helpText = kShadowResolutionHelpText,
                .valueMin = 1,
                .valueMax = 8,
                .defaultValue = 1,
            },
            mPrelaunch);

        // leftPane.add_section("Post-Processing");

        // leftPane.add_section("Rendering");
    });

    add_tab("Input", [this](Rml::Element *content) {
        auto &leftPane = add_child<Pane>(content, Pane::Type::Controlled);
        auto &rightPane = add_child<Pane>(content, Pane::Type::Uncontrolled);

        auto addOption = [&](const Rml::String &key, ConfigVar<bool> &value, const Rml::String &helpText, std::function<bool()> isDisabled = {}) {
            config_bool_select(leftPane, rightPane, value,
                {
                    .key = key,
                    .helpText = helpText,
                    .isDisabled = std::move(isDisabled),
                });
        };

        leftPane.add_section("Controller");
        leftPane.register_control(
            leftPane.add_button("Configure Controller").on_pressed([this] { push(std::make_unique<ControllerConfigWindow>()); }), rightPane,
            [](Pane &pane) {
                pane.clear();
                pane.add_text("Open controller binding configuration.");
            });
        config_bool_select(leftPane, rightPane, getSettings().game.allowBackgroundInput,
            {
                .key = "Allow Background Input",
                .helpText = "Allow controller input even when the game window is not focused.",
                .onChange = [](bool value) { aurora_set_background_input(value); },
            });
        leftPane.register_control(leftPane.add_select_button({
                                      .key = "Button Icons",
                                      .getValue =
                                          [] {
                                              const int idx = getSettings().game.buttonIcons.getValue();
                                              return Rml::String { kButtonIconNames[idx >= 0 && idx < PB_ICONS_SETTING_COUNT ? idx : 0] };
                                          },
                                      .isModified =
                                          [] {
                                              const auto &icons = getSettings().game.buttonIcons;
                                              return icons.getValue() != icons.getDefaultValue();
                                          },
                                  }),
            rightPane, [](Pane &pane) {
                for (int i = 0; i < static_cast<int>(kButtonIconNames.size()); ++i) {
                    pane.add_button({
                                        .text = kButtonIconNames[i],
                                        .isSelected = [i] { return getSettings().game.buttonIcons.getValue() == i; },
                                    })
                        .on_pressed([i] {
                            getSettings().game.buttonIcons.setValue(i);
                            config::Save();
                        });
                }
                pane.add_rml("<br/>The controller buttons the game's prompts show. Auto: each player's own controller "
                             "(PlayStation, Xbox, Switch, Steam Deck, else Xbox). Generic shows the buttons by position. "
                             "GameCube keeps the original icons.");
                for (int port = 0; port < 4; ++port) {
                    pane.add_rml(fmt::format("<br/>Port {}: {} {}", port + 1,
                        pb_button_icons_family_name(pb_button_icons_family(port)), button_glyph_strip_rml(port)));
                }
            });
        config_bool_select(leftPane, rightPane, getSettings().game.touchControls,
            {
                .key = "Touch Controls",
                .helpText = "Show an on-screen controller (stick, A/B/X/Y, L/R/Z, Start) for port 1 when the screen is "
                            "touched. A controller or keyboard press hides it again.",
            });
        config_bool_select(leftPane, rightPane, getSettings().game.touchHaptics,
            {
                .key = "Vibrate on Touch Buttons",
                .helpText = "A short vibration when an on-screen controller button is pressed, and a lighter one when "
                            "its stick moves into a new direction. Controllers' own buttons never vibrate.",
                .isDisabled = [] { return !getSettings().game.touchControls.getValue(); },
            });

        leftPane.add_section("Tools");
        addOption("Turbo Key", getSettings().game.enableTurboKeybind, "Hold Tab to unlock the FPS, speeding up the game.",
            [] { return getSettings().game.speedrunMode; });
    });

    add_tab("Audio", [this](Rml::Element *content) {
        auto &leftPane = add_child<Pane>(content, Pane::Type::Controlled);
        auto &rightPane = add_child<Pane>(content, Pane::Type::Uncontrolled);

        // N124: only Master Volume. audio.mainMusicVolume / soundEffectsVolume / ... stay unmapped: the game owns
        // msmMusSetMasterVolume / msmSeSetMasterVolume / msmStreamSetMasterVolume (sreset.c mutes all three,
        // selmenuDll toggles music 0/127) and would overwrite them. Reverb/HRTF have no backend here.
        leftPane.add_section("Volume");
        leftPane.register_control(
            leftPane.add_child<NumberButton>(NumberButton::Props {
                .key = "Master Volume",
                .getValue = [] { return getSettings().audio.masterVolume.getValue(); },
                .setValue =
                    [](int value) {
                        value = std::clamp(value, 0, 100);
                        getSettings().audio.masterVolume.setValue(value);
                        config::Save();
                        partyboard_audio_set_master_volume(value);
                    },
                .isModified =
                    [] {
                        return getSettings().audio.masterVolume.getValue() !=
                               getSettings().audio.masterVolume.getDefaultValue();
                    },
                .min = 0,
                .max = 100,
                .step = 5,
                .suffix = "%",
            }),
            rightPane, [](Pane &pane) {
                pane.clear();
                pane.add_text("Adjusts the volume of all sounds in the game. Takes effect right away.");
            });
    });

    add_tab("Gameplay", [this](Rml::Element *content) {
        auto &leftPane = add_child<Pane>(content, Pane::Type::Controlled);
        auto &rightPane = add_child<Pane>(content, Pane::Type::Uncontrolled);

        auto addOption = [&](const Rml::String &key, ConfigVar<bool> &value, const Rml::String &helpText) {
            config_bool_select(leftPane, rightPane, value,
                {
                    .key = key,
                    .helpText = helpText,
                });
        };
        auto addSpeedrunDisabledOption = [&](const Rml::String &key, ConfigVar<bool> &value, const Rml::String &helpText) {
            config_bool_select(leftPane, rightPane, value,
                {
                    .key = key,
                    .helpText = helpText,
                    .isDisabled = [] { return getSettings().game.speedrunMode; },
                });
        };

        // leftPane.add_section("General");
        // leftPane.add_section("Difficulty");
        // leftPane.add_section("Quality of Life");
        leftPane.add_section("Speedrunning");
        config_bool_select(leftPane, rightPane, getSettings().game.speedrunMode,
            {
                .key = "Speedrun Mode",
                .helpText = "Enables speedrunning options while restricting certain gameplay modifiers.",
                .onChange = [](bool) { reset_for_speedrun_mode(); },
            });
    });

    add_tab("Cheats", [this](Rml::Element *content) {
        auto &leftPane = add_child<Pane>(content, Pane::Type::Controlled);
        auto &rightPane = add_child<Pane>(content, Pane::Type::Uncontrolled);

        auto addCheat = [&](const Rml::String &key, ConfigVar<bool> &value, const Rml::String &helpText) {
            config_bool_select(leftPane, rightPane, value,
                {
                    .key = key,
                    .helpText = helpText,
                    .isDisabled = [] { return getSettings().game.speedrunMode; },
                });
        };

        leftPane.add_section("Minigames");
        addCheat("Unlock All Minigames", getSettings().game.unlockAllMinigames,
            "Treat every minigame as unlocked when the game checks minigame availability.");

        leftPane.add_section("Boards");
        addCheat("Unlock Bowser's Gnarly Party", getSettings().game.unlockBowsersGnarlyParty,
            "Treat Bowser's Gnarly Party as unlocked when the game checks board availability.");

        // leftPane.add_section("Resources");
        // addCheat("Infinite Hearts", getSettings().game.infiniteHearts, "Keeps your health full.");

        // leftPane.add_section("Abilities");
    });

    add_tab("Interface", [this](Rml::Element *content) {
        auto &leftPane = add_child<Pane>(content, Pane::Type::Controlled);
        auto &rightPane = add_child<Pane>(content, Pane::Type::Uncontrolled);

        leftPane.add_section("Party Board");
#if PARTY_BOARD_CAN_OPEN_DATA_FOLDER
        leftPane.register_control(leftPane.add_button("Open Data Folder").on_pressed([] {
            // mDoAud_seStartMenu(kSoundClick); // TODO PC
            partyboard::OpenDataFolder();
        }),
            rightPane, [](Pane &pane) {
                pane.add_text("Open the folder where Party Board stores settings, saves, logs, texture "
                              "replacements, and other app data.");
            });
#endif
        leftPane.register_control(leftPane.add_select_button({
                                      .key = "Notifications",
                                      .getValue =
                                          [] {
                                              const bool ach = getSettings().game.enableAchievementToasts.getValue();
                                              const bool ctl = getSettings().game.enableControllerToasts.getValue();
                                              if (!ach && !ctl) {
                                                  return Rml::String { "Off" };
                                              }
                                              if (ach && ctl) {
                                                  return Rml::String { "All" };
                                              }
                                              return Rml::String { "Some" };
                                          },
                                      .isModified =
                                          [] {
                                              const auto &ach = getSettings().game.enableAchievementToasts;
                                              const auto &ctl = getSettings().game.enableControllerToasts;
                                              return ach.getValue() != ach.getDefaultValue() || ctl.getValue() != ctl.getDefaultValue();
                                          },
                                  }),
            rightPane, [](Pane &pane) {
                pane.clear();
                pane.add_button("Select All").on_pressed([] {
                    // mDoAud_seStartMenu(kSoundItemChange); // TODO PC
                    getSettings().game.enableAchievementToasts.setValue(true);
                    getSettings().game.enableControllerToasts.setValue(true);
                    config::Save();
                });
                pane.add_button("Select None").on_pressed([] {
                    // mDoAud_seStartMenu(kSoundItemChange); // TODO PC
                    getSettings().game.enableAchievementToasts.setValue(false);
                    getSettings().game.enableControllerToasts.setValue(false);
                    config::Save();
                });

                pane.add_section("Types");
                pane.add_button({
                                    .text = "Achievements",
                                    .isSelected = [] { return getSettings().game.enableAchievementToasts.getValue(); },
                                })
                    .on_pressed([] {
                        // mDoAud_seStartMenu(kSoundItemChange); // TODO PC
                        auto &v = getSettings().game.enableAchievementToasts;
                        v.setValue(!v.getValue());
                        config::Save();
                    });
                pane.add_button({
                                    .text = "Controller",
                                    .isSelected = [] { return getSettings().game.enableControllerToasts.getValue(); },
                                })
                    .on_pressed([] {
                        // mDoAud_seStartMenu(kSoundItemChange); // TODO PC
                        auto &v = getSettings().game.enableControllerToasts;
                        v.setValue(!v.getValue());
                        config::Save();
                    });
                pane.add_rml("<br/>Choose which notifications can be displayed.");
            });
        config_bool_select(leftPane, rightPane, getSettings().backend.skipPreLaunchUI,
            {
                .key = "Skip Party Board Main Menu",
                .helpText = "When starting Party Board, skip the main menu and boot straight into the "
                            "game if a disc image is available.",
            });
        config_bool_select(leftPane, rightPane, getSettings().backend.skipBootSequence,
            {
                .key = "Skip Game Boot Sequence",
                .helpText = "Start directly at mode select after running the startup initialization normally "
                            "performed by the logos, title, and file select screens.",
            });
        // config_bool_select(leftPane, rightPane, getSettings().backend.showPipelineCompilation,
        //     {
        //         .key = "Show Pipeline Compilation",
        //         .helpText = "Show an overlay when shaders are being compiled for your hardware.",
        //     });
        config_bool_select(leftPane, rightPane, getSettings().game.pauseOnFocusLost,
            {
                .key = "Pause On Focus Lost",
                .helpText = "Pause the game when window focus is lost.",
                .onChange = [](bool value) { aurora_set_pause_on_focus_lost(value); },
            });
        // config_bool_select(leftPane, rightPane, getSettings().backend.enableAdvancedSettings,
        //     {
        //         .key = "Enable Advanced Settings",
        //         .icon = "warning",
        //         .helpText = "Show advanced settings and debugging tools with "
        //                     "Shift+F1.<br/><br/><icon class=\"warning\"/> WARNING: Debugging tools "
        //                     "can easily break your game. Do not use on a regular save!",
        //         .onChange =
        //             [](bool) {
        //                 for (auto &doc : get_document_stack()) {
        //                     if (dynamic_cast<MenuBar *>(doc.get())) {
        //                         doc = std::make_unique<MenuBar>();
        //                         break;
        //                     }
        //                 }
        //             },
        //     });

        // leftPane.add_section("Game");
        // config_bool_select(leftPane, rightPane, getSettings().game.recordingMode,
        //     {
        //         .key = "Recording Mode",
        //         .helpText = "Disables the game HUD and all background music.<br/><br/>Useful for "
        //                     "recording footage.",
        //     });
    });

    // N109
    if (logfile::supported()) {
        add_tab("Advanced", [this](Rml::Element *content) {
            auto &leftPane = add_child<Pane>(content, Pane::Type::Controlled);
            auto &rightPane = add_child<Pane>(content, Pane::Type::Uncontrolled);

            leftPane.add_section("Logging");
            const std::string logPath = logfile::log_path().string();
            config_bool_select(leftPane, rightPane, getSettings().backend.writeLogFile,
                {
                    .key = "Write Log File",
                    .helpText = "Write everything Party Board logs, and a report if it crashes, to:<br/><br/>" +
                                escape(logPath) +
                                "<br/><br/>The last 3 runs are kept as well (partyboard.1.log, ...). Changes take "
                                "effect the next time Party Board starts.",
                });
            leftPane.add_text(logPath);
            if (logfile::can_open_logs_dir()) {
                leftPane.register_control(leftPane.add_button("Open Logs Folder").on_pressed([] {
                    logfile::open_logs_dir();
                }),
                    rightPane, [](Pane &pane) {
                        pane.clear();
                        pane.add_text("Open the folder that holds Party Board's log files.");
                    });
            }
        });
    }
}

void SettingsWindow::update()
{
    if (mPrelaunch && top_document() == this) {
        try_push_verification_modal(*this);
    }

    Window::update();
}

} // namespace partyboard::ui
