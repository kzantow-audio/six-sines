/*
 * Six Sines
 *
 * A synth with audio rate modulation.
 *
 * Copyright 2024-2025, Paul Walker and Various authors, as described in the github
 * transaction log.
 *
 * This source repo is released under the MIT license, but has
 * GPL3 dependencies, as such the combined work will be
 * released under GPL3.
 *
 * The source code and license are at https://github.com/baconpaul/six-sines
 */

#ifndef BACONPAUL_SIX_SINES_UI_SIX_SINES_EDITOR_H
#define BACONPAUL_SIX_SINES_UI_SIX_SINES_EDITOR_H

#include <functional>
#include <utility>
#include <juce_gui_basics/juce_gui_basics.h>
#include "sst/jucegui/style/JUCELookAndFeelAdapter.h"

#include <sst/jucegui/components/NamedPanel.h>
#include <sst/jucegui/components/WindowPanel.h>
#include <sst/jucegui/components/Knob.h>
#include <sst/jucegui/components/ToolTip.h>
#include <sst/jucegui/components/ToggleButton.h>
#include <sst/jucegui/components/MenuButton.h>
#include <sst/jucegui/components/JogUpDownButton.h>
#include <sst/jucegui/components/VUMeter.h>
#include <sst/jucegui/accessibility/FocusDebugger.h>
#include <sst/jucegui/data/Continuous.h>

#include "synth/synth.h"
#include "presets/preset-manager.h"
#include "ui-defaults.h"

namespace jcmp = sst::jucegui::components;
namespace jdat = sst::jucegui::data;

namespace baconpaul::six_sines::ui
{
struct MainPanel;
struct MainSubPanel;
struct MatrixPanel;
struct MatrixSubPanel;
struct SelfSubPanel;
struct MixerPanel;
struct MixerSubPanel;
struct MainPanSubPanel;
struct FineTuneSubPanel;
struct PlayModeSubPanel;
struct SourcePanel;
struct SourceSubPanel;
struct MacroPanel;
struct Clipboard;
struct PresetDataBinding;

struct SixSinesEditor : jcmp::WindowPanel
{
    Patch patchCopy;
    ModMatrixConfig modMatrixConfig;

    Synth::audioToUIQueue_t &audioToUI;
    Synth::mainToAudioQueue_T &mainToAudio;
    const clap_host_t *clapHost{nullptr};

    SixSinesEditor(Synth::audioToUIQueue_t &atou, Synth::mainToAudioQueue_T &utoa,
                   const clap_host_t *ch);
    virtual ~SixSinesEditor();

    std::unique_ptr<sst::jucegui::style::LookAndFeelManager> lnf;
    void onStyleChanged() override;

    void paint(juce::Graphics &g) override;
    void resized() override;

    void idle();
    std::unique_ptr<juce::Timer> idleTimer;

    std::unique_ptr<Clipboard> clipboard;

    std::unique_ptr<jcmp::NamedPanel> singlePanel;
    void doSinglePanelHamburger();
    void activateHamburger(bool b);

    std::unique_ptr<MainPanel> mainPanel;
    std::unique_ptr<MainSubPanel> mainSubPanel;
    std::unique_ptr<MainPanSubPanel> mainPanSubPanel;
    std::unique_ptr<FineTuneSubPanel> fineTuneSubPanel;
    std::unique_ptr<PlayModeSubPanel> playModeSubPanel;

    std::unique_ptr<MatrixPanel> matrixPanel;
    std::unique_ptr<MatrixSubPanel> matrixSubPanel;
    std::unique_ptr<SelfSubPanel> selfSubPanel;

    std::unique_ptr<MixerPanel> mixerPanel;
    std::unique_ptr<MixerSubPanel> mixerSubPanel;

    std::unique_ptr<MacroPanel> macroPanel;

    std::unique_ptr<SourcePanel> sourcePanel;
    std::unique_ptr<SourceSubPanel> sourceSubPanel;

    std::unique_ptr<presets::PresetManager> presetManager;
    std::unique_ptr<PresetDataBinding> presetDataBinding;
    std::unique_ptr<jcmp::JogUpDownButton> presetButton;
    void showPresetPopup();
    void doLoadPatch();
    void doSavePatch();
    void postPatchChange(const std::string &displayName);
    void resetToDefault();
    void setPatchNameDisplay();
    void setPatchNameTo(const std::string &);
    std::unique_ptr<juce::FileChooser> fileChooser;

    std::unique_ptr<defaultsProvder_t> defaultsProvider;

    void setSkinFromDefaults();

    std::unique_ptr<jcmp::ToolTip> toolTip;
    void showTooltipOn(juce::Component *c);
    void updateTooltip(jdat::Continuous *c);
    void updateTooltip(jdat::Discrete *d);
    void hideTooltip();

    void popupMenuForContinuous(jcmp::ContinuousParamEditor *e);

    void hideAllSubPanels();
    std::unordered_map<uint32_t, juce::Component::SafePointer<juce::Component>> componentByID;
    std::unordered_map<uint32_t, std::function<void()>> componentRefreshByID;

    void setAndSendParamValue(uint32_t id, float value, bool notifyAudio = true,
                              bool includeBeginEnd = true);
    void setAndSendParamValue(const Param &p, float value, bool notifyAudio = true,
                              bool includeBeginEnd = true)
    {
        setAndSendParamValue(p.meta.id, value, notifyAudio, includeBeginEnd);
    }

    bool keyPressed(const juce::KeyPress &key) override;

    void showNavigationMenu();

    void setZoomFactor(float zf);
    float zoomFactor{1.0f};
    std::function<void(float)> onZoomChanged{nullptr};
    bool toggleDebug();

    static constexpr uint32_t edWidth{688}, edHeight{812};

    std::unique_ptr<jcmp::VUMeter> vuMeter;

    // To turn this on, recompile with it on in six-sines-editor.cpp
    void visibilityChanged() override;
    void parentHierarchyChanged() override;
    std::unique_ptr<sst::jucegui::accessibility::FocusDebugger> focusDebugger;

    std::unordered_map<juce::Component *, std::function<void()>> panelSelectGestureFor;

    float engineSR{0}, hostSR{0};

    void requestParamsFlush();
    const clap_host_params_t *clapParamsExtension{nullptr};

    // the name tells you about the intent. It just makes startup faster
    void sneakyStartupGrabFrom(Patch &other);
};

struct HasEditor
{
    SixSinesEditor &editor;
    HasEditor(SixSinesEditor &e) : editor(e) {}
};
} // namespace baconpaul::six_sines::ui
#endif
