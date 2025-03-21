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

#ifndef BACONPAUL_SIX_SINES_DSP_MATRIX_NODE_H
#define BACONPAUL_SIX_SINES_DSP_MATRIX_NODE_H

#include "sst/basic-blocks/modulators/DAHDSREnvelope.h"
#include "sst/basic-blocks/modulators/SimpleLFO.h"
#include "sst/basic-blocks/mechanics/block-ops.h"
#include "sst/basic-blocks/dsp/PanLaws.h"
#include "sst/basic-blocks/dsp/DCBlocker.h"
#include "dsp/op_source.h"
#include "dsp/node_support.h"
#include "synth/patch.h"
#include "synth/mono_values.h"

namespace baconpaul::six_sines
{
namespace mech = sst::basic_blocks::mechanics;

struct MatrixNodeFrom : public EnvelopeSupport<Patch::MatrixNode>,
                        public LFOSupport<MatrixNodeFrom, Patch::MatrixNode>,
                        public ModulationSupport<Patch::MatrixNode, MatrixNodeFrom>
{
    OpSource &onto, &from;

    const Patch::MatrixNode &matrixNode;
    const MonoValues &monoValues;
    const VoiceValues &voiceValues;
    const float &level, &activeV, &modmodeV, &rmScaleV, &lfoToDepth, &envToLevel, &overdriveV;
    MatrixNodeFrom(const Patch::MatrixNode &mn, OpSource &on, OpSource &fr, MonoValues &mv,
                   const VoiceValues &vv)
        : matrixNode(mn), monoValues(mv), voiceValues(vv), onto(on), from(fr), level(mn.level),
          modmodeV(mn.modulationMode), activeV(mn.active), EnvelopeSupport(mn, mv, vv),
          LFOSupport(mn, mv), lfoToDepth(mn.lfoToDepth), envToLevel(mn.envToLevel),
          overdriveV(mn.overdrive), ModulationSupport(mn, this, mv, vv),
          rmScaleV(mn.modulationScale)
    {
    }

    bool active{false};
    int modMode{0};
    int rmScale{0};
    float overdriveFactor{1.0};

    void attack()
    {
        resetModulation();
        envResetMod();
        lfoResetMod();

        active = activeV > 0.5;

        modMode = (int)std::round(modmodeV);
        rmScale = (int)std::round(rmScaleV);
        if (active)
        {
            bindModulation();
            calculateModulation();
            envAttack();
            lfoAttack();
            overdriveFactor = overdriveV > 0.5 ? 10.0 : 1.0;
            if (modMode == 3)
            {
                overdriveFactor = overdriveV > 0.5 ? 3.0 : 1.0;
            }
        }
    }

    void applyBlock()
    {
        if (!active)
            return;

        calculateModulation();
        envProcess();
        lfoProcess();
        float modlev alignas(16)[blockSize], mod alignas(16)[blockSize];

        // Construct the level which is lfo * lev + env * lev or + env * dept + lev
        if (lfoIsEnveloped)
        {
            mech::scale_by<blockSize>(env.outputCache, lfo.outputBlock);
        }

        auto l2d = lfoToDepth * lfoAtten;
        static float la{-1111};
        if (la != lfoAtten)
        {
            la = lfoAtten;
        }

        if (envIsMult)
        {
            auto e2d = level * depthAtten;
            for (int i = 0; i < blockSize; ++i)
            {
                modlev[i] = applyMod + l2d * lfo.outputBlock[i] + e2d * env.outputCache[i];
            }
        }
        else
        {
            auto e2d = envToLevel * depthAtten;
            for (int i = 0; i < blockSize; ++i)
            {
                modlev[i] = applyMod + level + l2d * lfo.outputBlock[i] + e2d * env.outputCache[i];
            }
        }

        if (modMode == 1)
        {
            // we want op * ( 1 - depth ) + op * rm * depth or
            // op * ( 1 + depth ( rm - 1 ) )
            // since the multiplier of depth is rmLevel and it starts at one that means
            if (rmScale == 2)
            {
                for (int i = 0; i < blockSize; ++i)
                {
                    onto.rmLevel[i] += modlev[i] * (0.5 * (from.output[i] + 1) - 1.0);
                }
            }
            else if (rmScale == 1)
            {
                for (int i = 0; i < blockSize; ++i)
                {
                    onto.rmLevel[i] += modlev[i] * (std::fabs(from.output[i]) - 1.0);
                }
            }
            else
            {
                for (int i = 0; i < blockSize; ++i)
                {
                    onto.rmLevel[i] += modlev[i] * (from.output[i] - 1.0);
                }
            }
        }
        else if (modMode == 2)
        {
            // linear FM. -1..1 with a 10x ocerdrivce
            mech::mul_block<blockSize>(modlev, from.output, mod);
            for (int j = 0; j < blockSize; ++j)
            {
                onto.fmAmount[j] += (overdriveFactor * mod[j]);
            }
        }
        else if (modMode == 3)
        {
            // expoential fm. if mod is 0...1 the result is 2^mod - 1
            mech::mul_block<blockSize>(modlev, from.output, mod);
            for (int j = 0; j < blockSize; ++j)
            {
                onto.fmAmount[j] += monoValues.twoToTheX.twoToThe(overdriveFactor * mod[j]) - 1.0;
            }
        }
        else
        {
            mech::mul_block<blockSize>(modlev, from.output, mod);

            for (int j = 0; j < blockSize; ++j)
            {
                onto.phaseInput[j] += (int32_t)((1 << 27) * (overdriveFactor * mod[j]));
            }
        }
    }
    float applyMod{0.f};
    float depthAtten{1.0};
    float lfoAtten{1.0};

    void resetModulation()
    {
        depthAtten = 1.f;
        lfoAtten = 1.f;
        applyMod = 0.f;
    }

    bool checkLfoUsed() { return lfoToDepth != 0 || lfoUsedAsModulationSource; }

    void calculateModulation()
    {
        resetModulation();
        envResetMod();
        lfoResetMod();

        if (!anySources)
            return;

        for (int i = 0; i < numModsPer; ++i)
        {
            if (sourcePointers[i] &&
                (int)matrixNode.modtarget[i].value != Patch::SelfNode::TargetID::NONE)
            {
                // targets: env depth atten, lfo dept atten, direct adjust, env attack, lfo rate
                auto dp = depthPointers[i];
                if (!dp)
                    continue;
                auto d = *dp;

                auto handled = envHandleModulationValue((int)matrixNode.modtarget[i].value, d,
                                                        sourcePointers[i]) ||
                               lfoHandleModulationValue((int)matrixNode.modtarget[i].value, d,
                                                        sourcePointers[i]);

                if (!handled)
                {
                    switch ((Patch::MatrixNode::TargetID)matrixNode.modtarget[i].value)
                    {
                    case Patch::MatrixNode::DIRECT:
                        applyMod += d * *sourcePointers[i];
                        break;
                    case Patch::MatrixNode::DEPTH_ATTEN:
                        depthAtten *= 1.0 - d * (1.0 - std::clamp(*sourcePointers[i], 0.f, 1.f));
                        break;
                    case Patch::MatrixNode::LFO_DEPTH_ATTEN:
                        lfoAtten *= 1.0 - d * (1.0 - std::clamp(*sourcePointers[i], 0.f, 1.f));
                        break;
                    default:
                        break;
                    }
                }
            }
        }
    }
};

struct MatrixNodeSelf : EnvelopeSupport<Patch::SelfNode>,
                        LFOSupport<MatrixNodeSelf, Patch::SelfNode>,
                        ModulationSupport<Patch::SelfNode, MatrixNodeSelf>
{
    OpSource &onto;

    const Patch::SelfNode &selfNode;
    const MonoValues &monoValues;
    const VoiceValues &voiceValues;

    const float &fbBase, &lfoToFB, &activeV, &envToFB, &overdriveV;
    MatrixNodeSelf(const Patch::SelfNode &sn, OpSource &on, MonoValues &mv, const VoiceValues &vv)
        : selfNode(sn), monoValues(mv), voiceValues(vv), onto(on), fbBase(sn.fbLevel),
          lfoToFB(sn.lfoToFB), activeV(sn.active), envToFB(sn.envToFB), overdriveV(sn.overdrive),
          EnvelopeSupport(sn, mv, vv), LFOSupport(sn, mv), ModulationSupport(sn, this, mv, vv){};
    bool active{true}, lfoMul{false};
    float overdriveFactor{1.0};

    void attack()
    {
        resetModulation();
        envResetMod();
        lfoResetMod();

        active = activeV > 0.5;
        if (active)
        {
            bindModulation();
            calculateModulation();
            envAttack();
            lfoAttack();
            overdriveFactor = overdriveV > 0.5 ? 10.0 : 1.0;
        }
    }
    void applyBlock()
    {
        if (!active)
            return;

        calculateModulation();
        envProcess();
        lfoProcess();
        if (lfoIsEnveloped)
        {
            mech::scale_by<blockSize>(env.outputCache, lfo.outputBlock);
        }

        float modlev alignas(16)[blockSize];

        auto l2f = lfoToFB * lfoAtten;

        if (envIsMult)
        {
            auto e2f = fbBase * depthAtten;

            for (int i = 0; i < blockSize; ++i)
            {
                modlev[i] = l2f * lfo.outputBlock[i] + e2f * env.outputCache[i] + fbMod;
            }
        }
        else
        {
            auto e2f = envToFB * depthAtten;

            for (int i = 0; i < blockSize; ++i)
            {
                modlev[i] = fbBase + l2f * lfo.outputBlock[i] + e2f * env.outputCache[i] + fbMod;
            }
        }
        for (int j = 0; j < blockSize; ++j)
        {
            onto.feedbackLevel[j] = (int32_t)((1 << 24) * modlev[j] * overdriveFactor);
        }
    }

    float fbMod{0.f};
    float depthAtten{1.0};
    float lfoAtten{1.0};

    void resetModulation()
    {
        depthAtten = 1.f;
        lfoAtten = 1.f;
        fbMod = 0.f;
    }
    void calculateModulation()
    {
        resetModulation();
        envResetMod();
        lfoResetMod();

        if (!anySources)
            return;

        for (int i = 0; i < numModsPer; ++i)
        {
            if (sourcePointers[i] &&
                (int)selfNode.modtarget[i].value != Patch::SelfNode::TargetID::NONE)
            {
                // targets: env depth atten, lfo dept atten, direct adjust, env attack, lfo rate
                auto dp = depthPointers[i];
                if (!dp)
                    continue;
                auto d = *dp;

                auto handled = envHandleModulationValue((int)selfNode.modtarget[i].value, d,
                                                        sourcePointers[i]) ||
                               lfoHandleModulationValue((int)selfNode.modtarget[i].value, d,
                                                        sourcePointers[i]);

                if (!handled)
                {
                    switch ((Patch::SelfNode::TargetID)selfNode.modtarget[i].value)
                    {
                    case Patch::SelfNode::DIRECT:
                        fbMod += d * *sourcePointers[i];
                        break;
                    case Patch::SelfNode::DEPTH_ATTEN:
                        depthAtten *= 1.0 - d * (1.0 - std::clamp(*sourcePointers[i], 0.f, 1.f));
                        break;
                    case Patch::SelfNode::LFO_DEPTH_ATTEN:
                        lfoAtten *= 1.0 - d * (1.0 - std::clamp(*sourcePointers[i], 0.f, 1.f));
                        break;
                    default:
                        break;
                    }
                }
            }
        }
    }

    bool checkLfoUsed() { return lfoToFB != 0 || lfoUsedAsModulationSource; }
};

struct MixerNode : EnvelopeSupport<Patch::MixerNode>,
                   LFOSupport<MixerNode, Patch::MixerNode>,
                   ModulationSupport<Patch::MixerNode, MixerNode>
{
    float output alignas(16)[2][blockSize];
    OpSource &from;

    const Patch::MixerNode &mixerNode;
    const MonoValues &monoValues;
    const VoiceValues &voiceValues;

    const float &level, &activeF, &pan, &lfoToLevel, &lfoToPan, &envToLevel;
    bool active{false};

    MixerNode(const Patch::MixerNode &mn, OpSource &f, MonoValues &mv, const VoiceValues &vv)
        : mixerNode(mn), monoValues(mv), voiceValues(vv), from(f), pan(mn.pan), level(mn.level),
          activeF(mn.active), lfoToLevel(mn.lfoToLevel), lfoToPan(mn.lfoToPan),
          envToLevel(mn.envToLevel), EnvelopeSupport(mn, mv, vv), LFOSupport(mn, mv),
          ModulationSupport(mn, this, mv, vv)
    {
        memset(output, 0, sizeof(output));
    }

    float doBlock{false};
    sst::basic_blocks::dsp::DCBlocker<blockSize> dcBlocker;

    void attack()
    {
        resetModulation();
        envResetMod();
        lfoResetMod();

        active = activeF > 0.5;
        memset(output, 0, sizeof(output));
        if (active)
        {
            bindModulation();
            calculateModulation();
            envAttack();
            lfoAttack();

            dcBlocker.reset();
            memset(output, 0, sizeof(output));

            auto wf = (SinTable::WaveForm)(from.waveForm);
            if (wf == SinTable::TX3 || wf == SinTable::TX4 || wf == SinTable::TX7 ||
                wf == SinTable::TX8 || wf == SinTable::SPIKY_TX4 || wf == SinTable::SPIKY_TX8)
            {
                doBlock = true;
            }
        }
    }

    bool checkLfoUsed()
    {
        auto used = lfoUsedAsModulationSource;
        used = used || (lfoToLevel != 0);
        used = used || (lfoToPan != 0);

        return used;
    }

    void renderBlock()
    {
        if (!active)
        {
            return;
        }

        float vSum alignas(16)[blockSize];

        calculateModulation();
        envProcess();

        lfoProcess();

        float dcValues alignas(16)[blockSize];
        float *useOut;
        if (from.rmAssigned || doBlock)
        {
            dcBlocker.filter(from.output, dcValues);
            useOut = dcValues;
        }
        else
        {
            useOut = from.output;
        }

        if (lfoIsEnveloped)
        {
            mech::scale_by<blockSize>(env.outputCache, lfo.outputBlock);
        }

        auto lv = std::clamp(level + levMod, 0.f, 1.f) * depthAtten;

        if (envIsMult)
        {
            for (int j = 0; j < blockSize; ++j)
            {
                // use mech blah
                auto amp = lv * env.outputCache[j];
                amp += lfoAtten * lfoToLevel * lfo.outputBlock[j];
                vSum[j] = amp * useOut[j];
            }
        }
        else
        {
            for (int j = 0; j < blockSize; ++j)
            {
                // use mech blah
                auto amp = lv + envToLevel * env.outputCache[j];
                amp += lfoAtten * lfoToLevel * lfo.outputBlock[j];
                vSum[j] = amp * useOut[j];
            }
        }

        auto pn =
            std::clamp(pan + lfoPanAtten * lfoToPan * lfo.outputBlock[blockSize - 1] +
                           (from.unisonParticipatesPan ? voiceValues.uniPanShift : 0.f) + panMod,
                       -1.f, 1.f);
        if (pn != 0.f)
        {
            pn = (pn + 1) * 0.5;
            sdsp::pan_laws::panmatrix_t pmat;
            sdsp::pan_laws::monoEqualPower(pn, pmat);

            mech::mul_block<blockSize>(vSum, pmat[0], output[0]);
            mech::mul_block<blockSize>(vSum, pmat[3], output[1]);
        }
        else
        {
            mech::copy_from_to<blockSize>(vSum, output[0]);
            mech::copy_from_to<blockSize>(vSum, output[1]);
        }

        // If I put this *here* then we get solo on running voices working
        if (mixerNode.isMutedDueToSoloAway)
        {
            memset(output, 0, sizeof(output));
            return;
        }
    }

    float levMod{0.f};
    float depthAtten{1.0};
    float lfoAtten{1.0};
    float lfoPanAtten{1.0};
    float panMod{0.0};

    void resetModulation()
    {
        depthAtten = 1.f;
        lfoAtten = 1.f;
        levMod = 0.f;
        panMod = 0.f;
        lfoPanAtten = 1.f;
    }

    void calculateModulation()
    {
        resetModulation();
        envResetMod();
        lfoResetMod();

        if (!anySources)
            return;

        for (int i = 0; i < numModsPer; ++i)
        {
            if (sourcePointers[i] &&
                (int)mixerNode.modtarget[i].value != Patch::MixerNode::TargetID::NONE)
            {
                // targets: env depth atten, lfo dept atten, direct adjust, env attack, lfo rate
                auto dp = depthPointers[i];
                if (!dp)
                    continue;
                auto d = *dp;

                auto handled = envHandleModulationValue((int)mixerNode.modtarget[i].value, d,
                                                        sourcePointers[i]) ||
                               lfoHandleModulationValue((int)mixerNode.modtarget[i].value, d,
                                                        sourcePointers[i]);

                if (!handled)
                {
                    switch ((Patch::MixerNode::TargetID)mixerNode.modtarget[i].value)
                    {
                    case Patch::MixerNode::DIRECT:
                        levMod += d * *sourcePointers[i];
                        break;
                    case Patch::MixerNode::PAN:
                        panMod += d * *sourcePointers[i];
                        break;
                    case Patch::MixerNode::DEPTH_ATTEN:
                        depthAtten *= 1.0 - d * (1.0 - std::clamp(*sourcePointers[i], 0.f, 1.f));
                        break;
                    case Patch::MixerNode::LFO_DEPTH_ATTEN:
                        lfoAtten *= 1.0 - d * (1.0 - std::clamp(*sourcePointers[i], 0.f, 1.f));
                        break;
                    case Patch::MixerNode::LFO_DEPTH_PAN_ATTEN:
                        lfoPanAtten *= 1.0 - d * (1.0 - std::clamp(*sourcePointers[i], 0.f, 1.f));
                        break;
                    default:
                        break;
                    }
                }
            }
        }
    }
};

struct MainPanNode : EnvelopeSupport<Patch::MainPanNode>,
                     ModulationSupport<Patch::MainPanNode, MainPanNode>,
                     LFOSupport<MainPanNode, Patch::MainPanNode>
{
    // float level alignas(16)[blockSize];
    float level{0.f};

    const Patch::MainPanNode &modNode;

    const MonoValues &monoValues;
    const VoiceValues &voiceValues;

    const float &lfoD, &envD;

    MainPanNode(const Patch::MainPanNode &mn, MonoValues &mv, const VoiceValues &vv)
        : ModulationSupport(mn, this, mv, vv), EnvelopeSupport(mn, mv, vv), LFOSupport(mn, mv),
          modNode(mn), monoValues(mv), voiceValues(vv), lfoD(mn.lfoDepth), envD(mn.envDepth)
    {
    }

    bool active{0};
    void attack()
    {
        resetModulation();
        envResetMod();
        lfoResetMod();

        bindModulation();

        active = true;
        if (std::fabs(lfoD) < 1e-8 && std::fabs(envD) < 1e-8)
        {
            active = false;
            for (auto &d : sourcePointers)
            {
                if (d)
                    active = true;
            }
        }

        level = 0.f;
        if (!active)
        {
            return;
        }

        calculateModulation();
        envAttack();
        lfoAttack();
    }

    void modProcess()
    {
        if (!active)
            return;

        calculateModulation();
        envProcess(true, false);
        lfoProcess();

        auto lfoLev = lfo.outputBlock[blockSize - 1];

        if (lfoIsEnveloped)
        {
            lfoLev = lfoLev * env.outBlock0;
        }

        level = directMod + env.outBlock0 * (envD + edMod) * envAtten +
                lfoLev * (lfoD + ldMod) * lfoAtten;
    }

    float lfoAtten{0.f};
    float directMod{0.f};
    float envAtten{1.0};
    float edMod{0.f};
    float ldMod{0.f};

    void resetModulation()
    {
        lfoAtten = 1.f;
        envAtten = 1.f;
        directMod = 0.f;
        edMod = 0.f;
        ldMod = 0.f;
    }
    void calculateModulation()
    {
        resetModulation();
        envResetMod();
        lfoResetMod();

        if (!anySources)
            return;

        for (int i = 0; i < numModsPer; ++i)
        {
            if (sourcePointers[i] &&
                (int)modNode.modtarget[i].value != Patch::MainPanNode::TargetID::NONE)
            {
                // targets: env depth atten, lfo dept atten, direct adjust, env attack, lfo rate
                auto dp = depthPointers[i];
                if (!dp)
                    continue;
                auto d = *dp;

                auto handled =
                    envHandleModulationValue((int)modNode.modtarget[i].value, d,
                                             sourcePointers[i]) ||
                    lfoHandleModulationValue((int)modNode.modtarget[i].value, d, sourcePointers[i]);

                if (!handled)
                {
                    switch ((Patch::MainPanNode::TargetID)modNode.modtarget[i].value)
                    {
                    case Patch::MainPanNode::DIRECT:
                        directMod += d * *sourcePointers[i];
                        break;
                    case Patch::MainPanNode::ENVDEP_DIR:
                        edMod += d * *sourcePointers[i];
                        break;
                    case Patch::MainPanNode::LFODEP_DIR:
                        ldMod += d * *sourcePointers[i];
                        break;
                    case Patch::MainPanNode::DEPTH_ATTEN:
                        envAtten *= 1.0 - d * (1.0 - std::clamp(*sourcePointers[i], 0.f, 1.f));
                        break;
                    case Patch::MainPanNode::LFO_DEPTH_ATTEN:
                        lfoAtten *= 1.0 - d * (1.0 - std::clamp(*sourcePointers[i], 0.f, 1.f));
                        break;
                    default:
                        break;
                    }
                }
            }
        }
    }

    bool checkLfoUsed() { return lfoD != 0 || lfoUsedAsModulationSource; }
};

struct FineTuneNode : EnvelopeSupport<Patch::FineTuneNode>,
                      ModulationSupport<Patch::FineTuneNode, FineTuneNode>,
                      LFOSupport<FineTuneNode, Patch::FineTuneNode>
{
    // float level alignas(16)[blockSize];
    float level{0.f}, coarseLevel{0.f};

    const Patch::FineTuneNode &modNode;

    const MonoValues &monoValues;
    const VoiceValues &voiceValues;

    const float &lfoD, &envD, &coarseTune, &lfoCoarseD, &envCoarseD;

    FineTuneNode(const Patch::FineTuneNode &mn, MonoValues &mv, const VoiceValues &vv)
        : ModulationSupport(mn, this, mv, vv), EnvelopeSupport(mn, mv, vv), LFOSupport(mn, mv),
          coarseTune(mn.coarseTune), modNode(mn), monoValues(mv), voiceValues(vv),
          lfoD(mn.lfoDepth), envD(mn.envDepth), lfoCoarseD(mn.lfoCoarseDepth),
          envCoarseD(mn.envCoarseDepth)
    {
    }

    bool active{0};
    void attack()
    {
        resetModulation();
        envResetMod();
        lfoResetMod();

        bindModulation();

        active = true;
        if (std::fabs(lfoD) < 1e-8 && std::fabs(envD) < 1e-8 && std::fabs(lfoCoarseD) < 1e-8 &&
            std::fabs(envCoarseD) < 1e-8)
        {
            active = false;
            for (auto &d : sourcePointers)
            {
                if (d)
                    active = true;
            }
        }

        level = 0.f;
        coarseLevel = 0.f;
        if (!active)
        {
            return;
        }

        calculateModulation();
        envAttack();
        lfoAttack();
    }

    void modProcess()
    {
        if (!active)
            return;

        calculateModulation();
        envProcess(true, false);
        lfoProcess();

        auto lfoLev = lfo.outputBlock[blockSize - 1];

        if (lfoIsEnveloped)
        {
            lfoLev = lfoLev * env.outBlock0;
        }

        level = directMod + env.outBlock0 * (envD + edMod) * envAtten +
                lfoLev * (lfoD + ldMod) * lfoAtten;

        coarseLevel = directCoarseMod + env.outBlock0 * (envCoarseD + edMod) * envAtten +
                      lfoLev * (lfoCoarseD + ldMod) * lfoAtten;
        coarseLevel *= 24;
    }

    float lfoAtten{0.f};
    float directMod{0.f};
    float directCoarseMod{0.f};
    float envAtten{1.0};
    float edMod{0.f};
    float ldMod{0.f};

    void resetModulation()
    {
        lfoAtten = 1.f;
        envAtten = 1.f;
        directMod = 0.f;
        directCoarseMod = 0.f;
        edMod = 0.f;
        ldMod = 0.f;
    }
    void calculateModulation()
    {
        resetModulation();
        envResetMod();
        lfoResetMod();

        if (!anySources)
            return;

        for (int i = 0; i < numModsPer; ++i)
        {
            if (sourcePointers[i] &&
                (int)modNode.modtarget[i].value != Patch::FineTuneNode::TargetID::NONE)
            {
                // targets: env depth atten, lfo dept atten, direct adjust, env attack, lfo rate
                auto dp = depthPointers[i];
                if (!dp)
                    continue;
                auto d = *dp;

                auto handled =
                    envHandleModulationValue((int)modNode.modtarget[i].value, d,
                                             sourcePointers[i]) ||
                    lfoHandleModulationValue((int)modNode.modtarget[i].value, d, sourcePointers[i]);

                if (!handled)
                {
                    switch ((int32_t)modNode.modtarget[i].value)
                    {
                    case Patch::FineTuneNode::DIRECT:
                        directMod += d * *sourcePointers[i];
                        break;
                    case Patch::FineTuneNode::ENVDEP_DIR:
                        edMod += d * *sourcePointers[i];
                        break;
                    case Patch::FineTuneNode::LFODEP_DIR:
                        ldMod += d * *sourcePointers[i];
                        break;
                    case Patch::FineTuneNode::DEPTH_ATTEN:
                        envAtten *= 1.0 - d * (1.0 - std::clamp(*sourcePointers[i], 0.f, 1.f));
                        break;
                    case Patch::FineTuneNode::LFO_DEPTH_ATTEN:
                        lfoAtten *= 1.0 - d * (1.0 - std::clamp(*sourcePointers[i], 0.f, 1.f));
                        break;
                    case Patch::FineTuneNode::COARSE:
                        directCoarseMod += d * *sourcePointers[i];
                        break;
                    default:
                        break;
                    }
                }
            }
        }
    }

    bool checkLfoUsed() { return lfoD != 0 || lfoCoarseD != 0 || lfoUsedAsModulationSource; }
};

struct OutputNode : EnvelopeSupport<Patch::OutputNode>,
                    ModulationSupport<Patch::OutputNode, OutputNode>,
                    LFOSupport<OutputNode, Patch::OutputNode>
{
    float output alignas(16)[2][blockSize];
    std::array<MixerNode, numOps> &fromArr;

    const Patch::OutputNode &outputNode;

    const MonoValues &monoValues;
    const VoiceValues &voiceValues;

    const float &level, &velSen, &bendUp, &bendDown, &octTranspose, &pan, &fineTune, &lfoDepth;
    const float &defTrigV;
    TriggerMode defaultTrigger;

    MainPanNode panModNode;
    FineTuneNode ftModNode;

    OutputNode(const Patch::OutputNode &on, const Patch::MainPanNode &panMN,
               const Patch::FineTuneNode &ftMN, std::array<MixerNode, numOps> &f, MonoValues &mv,
               const VoiceValues &vv)
        : outputNode(on), ModulationSupport(on, this, mv, vv), monoValues(mv), voiceValues(vv),
          fromArr(f), level(on.level), bendUp(on.bendUp), bendDown(on.bendDown),
          octTranspose(on.octTranspose), velSen(on.velSensitivity), EnvelopeSupport(on, mv, vv),
          LFOSupport(on, mv), defTrigV(on.defaultTrigger), pan(on.pan), fineTune(on.fineTune),
          lfoDepth(on.lfoDepth), ftModNode(ftMN, mv, vv), panModNode(panMN, mv, vv)
    {
        memset(output, 0, sizeof(output));
        allowVoiceTrigger = false;
    }

    void attack()
    {
        resetModulation();
        envResetMod();
        lfoResetMod();

        memset(output, 0, sizeof(output));

        defaultTrigger = (TriggerMode)std::round(defTrigV);
        bindModulation();
        calculateModulation();
        envAttack();
        lfoAttack();
        ftModNode.attack();
        panModNode.attack();
    }

    float finalEnvLevel alignas(16)[blockSize];
    void renderBlock()
    {
        calculateModulation();
        for (const auto &from : fromArr)
        {
            if (from.from.operatorOutputsToMain)
            {
                mech::accumulate_from_to<blockSize>(from.output[0], output[0]);
                mech::accumulate_from_to<blockSize>(from.output[1], output[1]);
            }
        }

        envProcess(false);
        lfoProcess();
        ftModNode.modProcess();
        panModNode.modProcess();

        mech::copy_from_to<blockSize>(env.outputCache, finalEnvLevel);
        mech::scale_by<blockSize>(depthAtten, finalEnvLevel);

        if (lfoIsEnveloped)
        {
            mech::scale_by<blockSize>(env.outputCache, lfo.outputBlock);
        }

        auto l2f = lfoDepth * lfoAtten;
        mech::mul_block<blockSize>(lfo.outputBlock, l2f, lfo.outputBlock);
        mech::accumulate_from_to<blockSize>(lfo.outputBlock, finalEnvLevel);

        // push this into final env level so we dont traverse output twice then clients can use it
        auto lv = std::clamp(level + levMod, 0.f, 1.f);
        auto v = 1.f - velSen * (1.f - voiceValues.velocityLag.v);
        lv = 0.15 * std::clamp(v * lv * lv * lv, 0.f, 1.f);
        mech::scale_by<blockSize>(lv, finalEnvLevel);

        mech::scale_by<blockSize>(finalEnvLevel, output[0], output[1]);

        auto pn = std::clamp(panMod + pan + panModNode.level + voiceValues.noteExpressionPanBipolar,
                             -1.f, 1.f);
        ;
        if (pn != 0.f)
        {
            pn = (pn + 1) * 0.5;
            sdsp::pan_laws::panmatrix_t pmat;
            sdsp::pan_laws::stereoEqualPower(pn, pmat);

            for (int i = 0; i < blockSize; ++i)
            {
                auto oL = output[0][i];
                auto oR = output[1][i];

                output[0][i] = pmat[0] * oL + pmat[2] * oR;
                output[1][i] = pmat[3] * oL + pmat[1] * oR;
            }
        }

#if DEBUG_LEVELS
        for (int i = 0; i < blockSize; ++i)
        {
            if (std::fabs(output[0][i]) > 1 || std::fabs(output[1][i]) > 1)
            {
                SXSNLOG(i << " " << output[0][i] << " " << output[1][i]);
            }
        }
#endif
    }

    float levMod{0.f};
    float panMod{0.f};
    float depthAtten{1.0};
    float lfoAtten{1.0};

    void resetModulation()
    {
        depthAtten = 1.f;
        panMod = 0.f;
        levMod = 0.f;
        lfoAtten = 1.f;
    }
    void calculateModulation()
    {
        resetModulation();
        envResetMod();
        lfoResetMod();

        if (!anySources)
            return;

        for (int i = 0; i < numModsPer; ++i)
        {
            if (sourcePointers[i] &&
                (int)outputNode.modtarget[i].value != Patch::OutputNode::TargetID::NONE)
            {
                // targets: env depth atten, lfo dept atten, direct adjust, env attack, lfo rate
                auto dp = depthPointers[i];
                if (!dp)
                    continue;
                auto d = *dp;

                auto handled = envHandleModulationValue((int)outputNode.modtarget[i].value, d,
                                                        sourcePointers[i]) ||
                               lfoHandleModulationValue((int)outputNode.modtarget[i].value, d,
                                                        sourcePointers[i]);

                if (!handled)
                {
                    switch ((Patch::OutputNode::TargetID)outputNode.modtarget[i].value)
                    {
                    case Patch::OutputNode::PAN:
                        panMod += d * *sourcePointers[i];
                        break;
                    case Patch::OutputNode::DIRECT:
                        levMod += d * *sourcePointers[i];
                        break;
                    case Patch::OutputNode::DEPTH_ATTEN:
                        depthAtten *= 1.0 - d * (1.0 - std::clamp(*sourcePointers[i], 0.f, 1.f));
                        break;
                    case Patch::OutputNode::LFO_DEPTH_ATTEN:
                        lfoAtten *= 1.0 - d * (1.0 - std::clamp(*sourcePointers[i], 0.f, 1.f));
                        break;
                    default:
                        break;
                    }
                }
            }
        }
    }

    bool checkLfoUsed() { return lfoDepth != 0 || lfoUsedAsModulationSource; }
};
} // namespace baconpaul::six_sines

#endif // MATRIX_NODE_H
