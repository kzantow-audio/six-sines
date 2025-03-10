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

#ifndef BACONPAUL_SIX_SINES_UI_MIXER_SUB_PANEL_H
#define BACONPAUL_SIX_SINES_UI_MIXER_SUB_PANEL_H

#include <juce_gui_basics/juce_gui_basics.h>
#include "six-sines-editor.h"
#include "dahdsr-components.h"
#include "lfo-components.h"
#include "modulation-components.h"
#include "clipboard.h"

namespace baconpaul::six_sines::ui
{
struct MixerSubPanel : juce::Component,
                       HasEditor,
                       DAHDSRComponents<MixerSubPanel, Patch::MixerNode>,
                       LFOComponents<MixerSubPanel, Patch::MixerNode>,
                       ModulationComponents<MixerSubPanel, Patch::MixerNode>,
                       SupportsClipboard
{
    MixerSubPanel(SixSinesEditor &);
    ~MixerSubPanel();

    void resized() override;

    void beginEdit() {}

    size_t index{0};
    void setSelectedIndex(int i);

    std::unique_ptr<jcmp::Knob> lfoToLevel;
    std::unique_ptr<PatchContinuous::cubic_t> lfoToLevelDA;

    std::unique_ptr<jcmp::Label> lfoToLevelL;

    std::unique_ptr<jcmp::Knob> lfoToPan;
    std::unique_ptr<PatchContinuous> lfoToPanD;
    std::unique_ptr<PatchContinuous::cubic_t> lfoToPanDA;
    std::unique_ptr<jcmp::Label> lfoToPanL;

    std::unique_ptr<jcmp::RuledLabel> modLabelE, modLabelL;
    std::unique_ptr<jcmp::Knob> envToLev;
    std::unique_ptr<PatchContinuous::cubic_t> envToLevDA;
    std::unique_ptr<jcmp::Label> envToLevL;

    std::unique_ptr<jcmp::MultiSwitch> envMul;
    std::unique_ptr<PatchDiscrete> envMulD;

    void setEnabledState();

    HAS_CLIPBOARD_SUPPORT;
};
} // namespace baconpaul::six_sines::ui
#endif // MIXER_SUB_PANE_H
