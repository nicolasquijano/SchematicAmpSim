#include "ProcessorCommon.h"

// Hands each module of the rack the parameters of its instance (audio thread).

double SasProcessor::moduleParam(sas::SlotRef slot, const char* baseId) const {
    const auto& names = RackState::paramIds(slot.type);
    const auto& cache = modParams_[std::size_t(slot.type)][std::size_t(slot.instance)];
    for (std::size_t i = 0; i < names.size() && i < cache.size(); ++i)
        if (names[i] == baseId) return double(cache[i]->load());
    return 0.0;
}

// Hands each module of the rack the parameters of its instance. Audio thread.
void SasProcessor::applyModuleParams(const sas::RackOrder& order) {
    using T = sas::ModuleType;
    double bpm = 0.0;  // the host tempo, asked once, when a delay wants it
    bool gateSeen = false;
    for (int i = 0; i < order.count; ++i) {
        const sas::SlotRef s = order.slots[std::size_t(i)];
        sas::Module* m = rack_.audioModule(s);
        if (!m) continue;
        auto p = [&](const char* id) { return moduleParam(s, id); };
        switch (s.type) {
            case T::Gate: {
                auto* g = static_cast<sas::GateModule*>(m);
                g->setParams(p(ids::gateOn) > 0.5, p(ids::gateThreshold), p(ids::gateRelease));
                if (!gateSeen) {
                    gateSeen = true;
                    gateGain_.store(float(g->gain()));
                }
                break;
            }
            case T::Compressor: {
                sas::CompControls c;
                c.sustain = p(ids::compSustain) / 10.0;
                c.blend = p(ids::compBlend) / 10.0;
                c.tone = p(ids::compTone) / 10.0;
                c.level = p(ids::compLevel) / 10.0;
                c.fastRelease = p(ids::compFast) > 0.5;
                c.model = std::clamp(int(p(ids::compModel) + 0.5), 0, sas::kCompModelCount - 1);
                static_cast<sas::CompressorModule*>(m)->setParams(p(ids::compOn) > 0.5, c);
                break;
            }
            case T::Overdrive:
                static_cast<sas::OverdriveModule*>(m)->setModel(sas::DriveModel(std::clamp(int(p(ids::tsModel) + 0.5), 0, sas::kDriveModelCount - 1)));
                static_cast<sas::OverdriveModule*>(m)->setParams(p(ids::tsOn) > 0.5, {p(ids::tsDrive) / 10.0, p(ids::tsTone) / 10.0, p(ids::tsLevel) / 10.0});
                break;
            case T::NamPedal: {
                auto* n = static_cast<sas::NamPedalModule*>(m);
                n->setModel(namPedals_[s.instance].active.load());
                n->setParams(p(ids::nmpOn) > 0.5, p(ids::nmpDrive), p(ids::nmpLevel));
                break;
            }
            case T::Phaser: {
                sas::PhaserParams q;
                q.on = p(ids::phOn) > 0.5;
                q.speedHz = p(ids::phSpeed);
                q.intense = p(ids::phIntense) > 0.5;
                q.stereo = p(ids::phStereo) > 0.5;
                q.mix = p(ids::phMix) / 100.0;
                static_cast<sas::PhaserModule*>(m)->setParams(q);
                break;
            }
            case T::Chorus: {
                sas::ChorusParams q;
                q.on = p(ids::chOn) > 0.5;
                q.juno = p(ids::chMode) > 0.5 && p(ids::chMode) < 1.5;
                q.jc = p(ids::chMode) > 1.5;
                q.rateHz = p(ids::chRate);
                q.depth = p(ids::chDepth) / 100.0;
                q.junoButton = int(p(ids::chJuno) + 0.5);
                q.stereo = p(ids::chStereo) > 0.5;
                q.mix = p(ids::chMix) / 100.0;
                static_cast<sas::ChorusModule*>(m)->setParams(q);
                break;
            }
            case T::Vibe: {
                sas::VibeParams q;
                q.on = p(ids::vbOn) > 0.5;
                q.speedHz = p(ids::vbSpeed);
                q.intensity = p(ids::vbIntensity) / 100.0;
                q.vibrato = p(ids::vbVibrato) > 0.5;
                q.stereo = p(ids::vbStereo) > 0.5;
                q.mix = p(ids::vbMix) / 100.0;
                static_cast<sas::VibeModule*>(m)->setParams(q);
                break;
            }
            case T::Tremolo: {
                sas::TremoloParams q;
                q.on = p(ids::trOn) > 0.5;
                q.rateHz = p(ids::trRate);
                q.depth = p(ids::trDepth) / 100.0;
                q.shape = int(p(ids::trShape) + 0.5);
                q.stereo = p(ids::trStereo) > 0.5;
                static_cast<sas::TremoloModule*>(m)->setParams(q);
                break;
            }
            case T::Flanger: {
                sas::FlangerParams q;
                q.on = p(ids::flOn) > 0.5;
                q.rateHz = p(ids::flRate);
                q.depth = p(ids::flDepth) / 100.0;
                q.manual = p(ids::flManual) / 100.0;
                q.feedback = p(ids::flFeedback) / 100.0;
                q.stereo = p(ids::flStereo) > 0.5;
                q.mix = p(ids::flMix) / 100.0;
                static_cast<sas::FlangerModule*>(m)->setParams(q);
                break;
            }
            case T::Distortion: {
                auto* d = static_cast<sas::DistortionModule*>(m);
                d->setParams(p(ids::dstOn) > 0.5, {p(ids::dstDrive) / 10.0, p(ids::dstTone) / 10.0, p(ids::dstLevel) / 10.0});
                break;
            }
            case T::Layers: {
                sas::LayersParams q;
                q.on = p(ids::lyOn) > 0.5;
                q.mix = p(ids::lyMix) / 10.0;
                q.attackS = 0.02 * std::pow(150.0, p(ids::lyAttack) / 10.0);
                q.decayS = 0.3 * std::pow(46.7, p(ids::lyDecay) / 10.0);
                q.trig = p(ids::lyTrig) / 10.0;
                q.diffusion = p(ids::lyDiffusion) / 10.0;
                q.single = p(ids::lySingle) > 0.5;
                q.subOct = p(ids::lySub) > 0.5;
                q.hold = p(ids::lyHold) > 0.5;
                q.stereo = p(ids::lyStereo) > 0.5;
                static_cast<sas::LayersModule*>(m)->setParams(q);
                break;
            }
            case T::Bitcrusher: {
                sas::BitcrusherParams q;
                q.on = p(ids::bcOn) > 0.5;
                q.bits = p(ids::bcBits);
                q.rateHz = p(ids::bcRate);
                q.antiAlias = p(ids::bcAntiAlias) > 0.5;
                q.toneHz = p(ids::bcTone);
                q.dither = p(ids::bcDither) / 100.0;
                q.jitter = p(ids::bcJitter) / 100.0;
                q.mix = p(ids::bcMix) / 100.0;
                static_cast<sas::BitcrusherModule*>(m)->setParams(q);
                break;
            }
            case T::BbdDelay: {
                sas::BbdParams q;
                q.on = p(ids::bbdOn) > 0.5;
                q.timeMs = p(ids::bbdTime);
                q.feedback = p(ids::bbdFeedback) / 100.0;
                q.mix = p(ids::bbdMix) / 100.0;
                q.toneHz = p(ids::bbdTone);
                q.rateHz = p(ids::bbdRate);
                q.depth = p(ids::bbdDepth) / 100.0;
                q.grit = p(ids::bbdGrit) / 100.0;
                q.stereo = p(ids::bbdStereo) > 0.5;
                static_cast<sas::BbdDelayModule*>(m)->setParams(q);
                break;
            }
            case T::TapeEcho: {
                sas::TapeParams q;
                q.on = p(ids::tpOn) > 0.5;
                q.timeMs = p(ids::tpTime);
                q.heads = int(p(ids::tpHeads) + 0.5);
                q.intensity = p(ids::tpIntensity) / 100.0;
                q.mix = p(ids::tpMix) / 100.0;
                q.toneHz = p(ids::tpTone);
                q.wow = p(ids::tpWow) / 100.0;
                q.flutter = p(ids::tpFlutter) / 100.0;
                q.saturation = p(ids::tpSat) / 100.0;
                q.stereo = p(ids::tpStereo) > 0.5;
                static_cast<sas::TapeEchoModule*>(m)->setParams(q);
                break;
            }
            case T::Leslie: {
                sas::LeslieParams q;
                q.on = p(ids::lesOn) > 0.5;
                q.speed = int(p(ids::lesSpeed) + 0.5);
                q.depth = p(ids::lesDepth) / 10.0;
                q.balance = p(ids::lesBalance) / 10.0;
                q.width = p(ids::lesWidth) / 10.0;
                q.mix = p(ids::lesMix) / 100.0;
                static_cast<sas::LeslieModule*>(m)->setParams(q);
                break;
            }
            case T::SpringReverb: {
                sas::SpringReverbParams q;
                q.on = p(ids::spOn) > 0.5;
                q.type = int(p(ids::spType) + 0.5);
                q.tone = p(ids::spTone) / 10.0;
                q.mix = p(ids::spMix) / 100.0;
                static_cast<sas::SpringReverbModule*>(m)->setParams(q);
                break;
            }
            case T::Klon: {
                sas::KlonControls q;
                q.gain = p(ids::klnGain) / 10.0;
                q.treble = p(ids::klnTreble) / 10.0;
                q.level = p(ids::klnLevel) / 10.0;
                static_cast<sas::KlonModule*>(m)->setParams(p(ids::klnOn) > 0.5, q);
                break;
            }
            case T::Acoustic: {
                sas::AcousticParams q;
                q.on = p(ids::acOn) > 0.5;
                q.type = int(p(ids::acType) + 0.5);
                q.body = p(ids::acBody) / 10.0;
                q.top = p(ids::acTop) / 10.0;
                q.air = p(ids::acAir) / 10.0;
                q.squash = p(ids::acSquash) / 10.0;
                q.mix = p(ids::acMix) / 100.0;
                static_cast<sas::AcousticModule*>(m)->setParams(q);
                break;
            }
            case T::Boost: {
                sas::BoostParams q;
                q.on = p(ids::bstOn) > 0.5;
                q.gainDb = p(ids::bstGain);
                q.tone = p(ids::bstTone) / 10.0;
                q.softClip = p(ids::bstSoft) > 0.5;
                static_cast<sas::BoostModule*>(m)->setParams(q);
                break;
            }
            case T::Harmonist: {
                sas::HarmonistParams q;
                q.on = p(ids::harOn) > 0.5;
                q.mode = int(p(ids::harMode) + 0.5);
                q.key = int(p(ids::harKey) + 0.5);
                q.scale = int(p(ids::harScale) + 0.5);
                q.interval1 = p(ids::harInt1);
                q.interval2 = p(ids::harInt2);
                q.detuneCents = p(ids::harDetune);
                q.direct = p(ids::harDirect) / 10.0;
                q.voice1 = p(ids::harVoice1) / 10.0;
                q.voice2 = p(ids::harVoice2) / 10.0;
                static_cast<sas::HarmonistModule*>(m)->setParams(q);
                break;
            }
            case T::Octaver: {
                sas::OctaverParams q;
                q.on = p(ids::octOn) > 0.5;
                q.direct = p(ids::octDirect) / 10.0;
                q.oct1 = p(ids::octOct1) / 10.0;
                q.oct2 = p(ids::octOct2) / 10.0;
                q.tone = p(ids::octTone) / 10.0;
                static_cast<sas::OctaverModule*>(m)->setParams(q);
                break;
            }
            case T::TenBandEq: {
                sas::TenBandEqParams q;
                q.on = p(ids::tenOn) > 0.5;
                for (int b = 0; b < 10; ++b) q.bandsDb[std::size_t(b)] = p(ids::tenBand[b]);
                q.gainDb = p(ids::tenGain);
                q.volumeDb = p(ids::tenVolume);
                static_cast<sas::TenBandEqModule*>(m)->setParams(q);
                break;
            }
            case T::Limiter: {
                sas::LimiterParams q;
                q.on = p(ids::limOn) > 0.5;
                q.gainDb = p(ids::limGain);
                q.ceilingDb = p(ids::limCeiling);
                q.releaseMs = p(ids::limRelease);
                static_cast<sas::LimiterModule*>(m)->setParams(q);
                break;
            }
            case T::Fuzz: {
                sas::FuzzParams q;
                q.on = p(ids::fzOn) > 0.5;
                q.type = int(p(ids::fzType) + 0.5);
                q.fuzz = p(ids::fzFuzz) / 10.0;
                q.tone = p(ids::fzTone) / 10.0;
                q.bias = p(ids::fzBias) / 10.0;
                q.mix = p(ids::fzMix) / 100.0;
                q.levelDb = p(ids::fzLevel);
                static_cast<sas::FuzzModule*>(m)->setParams(q);
                break;
            }
            case T::Wah: {
                sas::WahParams q;
                q.on = p(ids::wahOn) > 0.5;
                q.mode = int(p(ids::wahMode) + 0.5);
                q.filter = int(p(ids::wahFilter) + 0.5);
                q.position = p(ids::wahPos) / 100.0;
                q.heelHz = p(ids::wahHeel);
                q.toeHz = p(ids::wahToe);
                q.q = p(ids::wahQ);
                q.mix = p(ids::wahMix) / 100.0;
                q.levelDb = p(ids::wahLevel);
                q.sensitivity = p(ids::wahSens) / 100.0;
                q.attackMs = p(ids::wahAttack);
                q.releaseMs = p(ids::wahRelease);
                q.down = p(ids::wahDir) > 0.5;
                q.rateHz = p(ids::wahRate);
                q.stereo = p(ids::wahStereo) > 0.5;
                static_cast<sas::WahModule*>(m)->setParams(q);
                break;
            }
            case T::Pitch:
                static_cast<sas::PitchModule*>(m)->setSemitones(p(ids::pitch) + p(ids::pitchFine) / 100.0);
                break;
            case T::Eq: {
                sas::EqParams q;
                q.on = p(ids::eqOn) > 0.5;
                q.hpfHz = p(ids::eqHpf);
                q.lpfHz = p(ids::eqLpf);
                for (int b = 0; b < sas::EqParams::kBands; ++b) {
                    q.band[std::size_t(b)].freq = p(ids::eqFreq[b]);
                    q.band[std::size_t(b)].gainDb = p(ids::eqGain[b]);
                    q.band[std::size_t(b)].q = p(ids::eqQ[b]);
                }
                static_cast<sas::EqModule*>(m)->setParams(q);
                break;
            }
            case T::Delay: {
                sas::DelayParams d;
                d.on = p(ids::dlyOn) > 0.5;
                d.timeMs = p(ids::dlyTime);
                if (const int sync = int(p(ids::dlySync) + 0.5); sync > 0) {
                    // Tied to the host tempo (120 bpm when the host does not say): a quarter note, a dotted eighth, an eighth, a sixteenth.
                    if (bpm <= 0.0) {
                        bpm = 120.0;
                        if (auto* head = getPlayHead())
                            if (auto pos = head->getPosition())
                                if (auto b = pos->getBpm()) bpm = std::clamp(*b, 30.0, 300.0);
                    }
                    static constexpr double kBeats[] = {1.0, 1.0, 0.75, 0.5, 0.25};
                    d.timeMs = std::clamp(60000.0 / bpm * kBeats[std::min(sync, 4)], 20.0, sas::StereoDelay::kMaxMs);
                }
                d.feedback = std::clamp(p(ids::dlyFeedback) / 100.0, 0.0, 0.92);
                d.mix = std::clamp(p(ids::dlyMix) / 100.0, 0.0, 1.0);
                d.toneHz = p(ids::dlyTone);
                d.pingPong = p(ids::dlyPing) > 0.5;
                d.analog = p(ids::dlyAnalog) > 0.5;
                d.stereo = p(ids::dlyStereo) > 0.5;
                static_cast<sas::DelayModule*>(m)->setParams(d);
                break;
            }
            case T::Reverb: {
                sas::PlateParams pl;
                pl.on = p(ids::plOn) > 0.5;
                pl.hall = p(ids::plHall) > 0.5;
                pl.decaySec = p(ids::plDecay);
                pl.preMs = p(ids::plPre);
                pl.toneHz = p(ids::plTone);
                pl.lowCutHz = p(ids::plLow);
                pl.mix = std::clamp(p(ids::plMix) / 100.0, 0.0, 1.0);
                pl.mod = std::clamp(p(ids::plMod) / 100.0, 0.0, 1.0);
                pl.shimmer = std::clamp(p(ids::plShimmer) / 100.0, 0.0, 1.0);
                pl.shimmerFifth = p(ids::plShimmerInt) > 0.5;
                pl.stereo = p(ids::plStereo) > 0.5;
                static_cast<sas::ReverbModule*>(m)->setParams(pl);
                break;
            }
            case T::Mixer: {
                sas::MixerParams mp;
                mp.levelA = float(std::pow(10.0, p(ids::mixLevelA) / 20.0));
                mp.levelB = float(std::pow(10.0, p(ids::mixLevelB) / 20.0));
                mp.panA = float(p(ids::mixPanA) / 100.0);
                mp.panB = float(p(ids::mixPanB) / 100.0);
                mp.invertB = p(ids::mixInvertB) > 0.5;
                static_cast<sas::MixerModule*>(m)->setParams(mp);
                break;
            }
            default: break;
        }
    }
    if (!gateSeen) gateGain_.store(1.0f);
}

