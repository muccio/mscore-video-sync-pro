#include "CueMarkerModel.h"
#include "../Sync/TransportSyncEngine.h"

namespace MScoreVideoSync
{

CueMarkerModel::CueMarkerModel()
{
}

void CueMarkerModel::addListener(Listener* listener)
{
    listeners_.add(listener);
}

void CueMarkerModel::removeListener(Listener* listener)
{
    listeners_.remove(listener);
}

void CueMarkerModel::notifyListeners()
{
    listeners_.call([](Listener& l) { l.markersChanged(); });
}

void CueMarkerModel::sortMarkersInternal()
{
    std::sort(markers_.begin(), markers_.end(), [](const CueMarker& a, const CueMarker& b) {
        return a.timeSeconds < b.timeSeconds;
    });
}

int CueMarkerModel::addMarker(const CueMarker& marker)
{
    int assignedId;
    {
        std::lock_guard<std::mutex> lock(markersMutex_);
        CueMarker m = marker;
        if (m.id <= 0)
        {
            m.id = nextMarkerId_++;
        }
        else
        {
            nextMarkerId_ = std::max(nextMarkerId_, m.id + 1);
        }
        assignedId = m.id;
        markers_.push_back(m);
        sortMarkersInternal();
    }
    notifyListeners();
    return assignedId;
}

int CueMarkerModel::addQuickMarker(double timeSeconds, double ppq, int bar, double beat, const juce::String& name)
{
    CueMarker m;
    m.timeSeconds = timeSeconds;
    m.ppqPosition = ppq;
    m.bar = bar;
    m.beat = beat;
    m.anchorType = MarkerAnchorType::TimecodeLocked;

    static const juce::Colour palette[] = {
        juce::Colour::fromRGB(0, 210, 255), // Cyan
        juce::Colour::fromRGB(255, 175, 40), // Amber
        juce::Colour::fromRGB(255, 60, 60),  // Red
        juce::Colour::fromRGB(0, 230, 130), // Green
        juce::Colour::fromRGB(180, 100, 255) // Purple
    };

    int count = getNumMarkers();
    m.color = palette[count % 5];
    m.name = name.isNotEmpty() ? name : ("Cue " + juce::String(count + 1));

    return addMarker(m);
}

bool CueMarkerModel::removeMarker(int markerId)
{
    bool removed = false;
    {
        std::lock_guard<std::mutex> lock(markersMutex_);
        auto it = std::remove_if(markers_.begin(), markers_.end(), [markerId](const CueMarker& m) {
            return m.id == markerId;
        });
        if (it != markers_.end())
        {
            markers_.erase(it, markers_.end());
            removed = true;
        }
    }
    if (removed)
    {
        notifyListeners();
    }
    return removed;
}

void CueMarkerModel::clearMarkers()
{
    {
        std::lock_guard<std::mutex> lock(markersMutex_);
        markers_.clear();
    }
    notifyListeners();
}

bool CueMarkerModel::updateMarker(const CueMarker& marker)
{
    bool found = false;
    {
        std::lock_guard<std::mutex> lock(markersMutex_);
        for (auto& m : markers_)
        {
            if (m.id == marker.id)
            {
                m = marker;
                found = true;
                break;
            }
        }
        if (found)
        {
            sortMarkersInternal();
        }
    }
    if (found)
    {
        notifyListeners();
    }
    return found;
}

std::vector<CueMarker> CueMarkerModel::getMarkersSorted() const
{
    std::lock_guard<std::mutex> lock(markersMutex_);
    return markers_;
}

bool CueMarkerModel::getMarkerById(int markerId, CueMarker& outMarker) const
{
    std::lock_guard<std::mutex> lock(markersMutex_);
    for (const auto& m : markers_)
    {
        if (m.id == markerId)
        {
            outMarker = m;
            return true;
        }
    }
    return false;
}

int CueMarkerModel::getNumMarkers() const
{
    std::lock_guard<std::mutex> lock(markersMutex_);
    return static_cast<int>(markers_.size());
}

bool CueMarkerModel::getActiveCue(double currentPts, double bpm, CueMarker& outCurrentCue, CueMarker& outNextCue, double& outCountdownSec) const
{
    std::lock_guard<std::mutex> lock(markersMutex_);
    if (markers_.empty())
        return false;

    bool hasCurrent = false;
    bool hasNext = false;
    outCountdownSec = -1.0;

    for (size_t i = 0; i < markers_.size(); ++i)
    {
        double pts = markers_[i].getEffectivePts(bpm, 4);
        if (pts <= currentPts && (currentPts - pts) < 3.0)
        {
            outCurrentCue = markers_[i];
            hasCurrent = true;
        }

        if (pts > currentPts && !hasNext)
        {
            outNextCue = markers_[i];
            outCountdownSec = pts - currentPts;
            hasNext = true;
        }
    }

    return hasCurrent || hasNext;
}

void CueMarkerModel::updateTempoMap(double bpm, int timeSigNum, int timeSigDenom)
{
    if (bpm <= 0.0) bpm = 120.0;
    bool changed = false;

    {
        std::lock_guard<std::mutex> lock(markersMutex_);
        for (auto& m : markers_)
        {
            if (m.anchorType == MarkerAnchorType::TimecodeLocked)
            {
                // SMPTE frame is fixed; recalculate musical position
                m.ppqPosition = (m.timeSeconds * bpm) / 60.0;
                TransportSyncEngine::calculateBarBeat(m.ppqPosition, timeSigNum, timeSigDenom, m.bar, m.beat);
                changed = true;
            }
            else
            {
                // Musical position is fixed; recalculate timecode in seconds
                m.timeSeconds = (m.ppqPosition * 60.0) / bpm;
                changed = true;
            }
        }
        if (changed)
        {
            sortMarkersInternal();
        }
    }

    if (changed)
    {
        notifyListeners();
    }
}

juce::var CueMarkerModel::exportToJson() const
{
    std::lock_guard<std::mutex> lock(markersMutex_);
    juce::var list;
    for (const auto& m : markers_)
    {
        auto* obj = new juce::DynamicObject();
        obj->setProperty("id", m.id);
        obj->setProperty("name", m.name);
        obj->setProperty("anchorType", static_cast<int>(m.anchorType));
        obj->setProperty("timeSeconds", m.timeSeconds);
        obj->setProperty("ppqPosition", m.ppqPosition);
        obj->setProperty("bar", m.bar);
        obj->setProperty("beat", m.beat);
        obj->setProperty("color", m.color.toDisplayString(true));
        obj->setProperty("streamerEnabled", m.streamerEnabled);
        obj->setProperty("streamerDurationType", static_cast<int>(m.streamerDurationType));
        obj->setProperty("streamerDurationSeconds", m.streamerDurationSeconds);
        obj->setProperty("streamerDurationBeats", m.streamerDurationBeats);
        obj->setProperty("streamerWidth", m.streamerWidth);
        obj->setProperty("punchEnabled", m.punchEnabled);
        obj->setProperty("punchFrameDuration", m.punchFrameDuration);
        obj->setProperty("punchColor", m.punchColor.toDisplayString(true));
        obj->setProperty("flutterEnabled", m.flutterEnabled);
        obj->setProperty("flutterFlashCount", m.flutterFlashCount);
        obj->setProperty("twoPopAudioEnabled", m.twoPopAudioEnabled);

        list.append(juce::var(obj));
    }
    return list;
}

bool CueMarkerModel::importFromJson(const juce::var& json)
{
    if (!json.isArray())
        return false;

    std::vector<CueMarker> newMarkers;
    const auto* arr = json.getArray();
    if (arr == nullptr) return false;

    for (const auto& item : *arr)
    {
        CueMarker m;
        m.id = item.getProperty("id", 0);
        m.name = item.getProperty("name", "Marker").toString();
        m.anchorType = static_cast<MarkerAnchorType>(static_cast<int>(item.getProperty("anchorType", 0)));
        m.timeSeconds = item.getProperty("timeSeconds", 0.0);
        m.ppqPosition = item.getProperty("ppqPosition", 0.0);
        m.bar = item.getProperty("bar", 1);
        m.beat = item.getProperty("beat", 1.0);
        m.color = juce::Colour::fromString(item.getProperty("color", "FF00D2FF").toString());
        m.streamerEnabled = item.getProperty("streamerEnabled", true);
        m.streamerDurationType = static_cast<StreamerDurationType>(static_cast<int>(item.getProperty("streamerDurationType", 0)));
        m.streamerDurationSeconds = item.getProperty("streamerDurationSeconds", 2.0);
        m.streamerDurationBeats = item.getProperty("streamerDurationBeats", 4.0);
        m.streamerWidth = static_cast<float>(item.getProperty("streamerWidth", 6.0));
        m.punchEnabled = item.getProperty("punchEnabled", true);
        m.punchFrameDuration = item.getProperty("punchFrameDuration", 2);
        m.punchColor = juce::Colour::fromString(item.getProperty("punchColor", "FFFFFFFF").toString());
        m.flutterEnabled = item.getProperty("flutterEnabled", false);
        m.flutterFlashCount = item.getProperty("flutterFlashCount", 5);
        m.twoPopAudioEnabled = item.getProperty("twoPopAudioEnabled", false);

        newMarkers.push_back(m);
    }

    {
        std::lock_guard<std::mutex> lock(markersMutex_);
        markers_ = std::move(newMarkers);
        sortMarkersInternal();
    }
    notifyListeners();
    return true;
}

juce::String CueMarkerModel::exportToCsv(SMPTEFrameRate smpteRate) const
{
    std::lock_guard<std::mutex> lock(markersMutex_);
    juce::String csv = "ID,Name,Timecode,Seconds,Bar,Beat,AnchorType,Streamer,Punch,TwoPop\n";

    for (const auto& m : markers_)
    {
        SMPTETimecode tc = SMPTETimecode::fromSeconds(m.timeSeconds, smpteRate);
        juce::String line = juce::String::formatted(
            "%d,\"%s\",%s,%.3f,%d,%.2f,%s,%s,%s,%s\n",
            m.id,
            m.name.toRawUTF8(),
            tc.toString(false).toRawUTF8(),
            m.timeSeconds,
            m.bar,
            m.beat,
            (m.anchorType == MarkerAnchorType::TimecodeLocked ? "SMPTE" : "Musical"),
            (m.streamerEnabled ? "YES" : "NO"),
            (m.punchEnabled ? "YES" : "NO"),
            (m.twoPopAudioEnabled ? "YES" : "NO")
        );
        csv += line;
    }
    return csv;
}

bool CueMarkerModel::importFromCsv(const juce::String& csvData, SMPTEFrameRate smpteRate)
{
    auto lines = juce::StringArray::fromLines(csvData);
    if (lines.isEmpty()) return false;

    std::vector<CueMarker> newMarkers;
    for (int i = 1; i < lines.size(); ++i) // skip header
    {
        auto row = lines[i].trim();
        if (row.isEmpty()) continue;

        auto cols = juce::StringArray::fromTokens(row, ",", "\"");
        if (cols.size() >= 4)
        {
            CueMarker m;
            m.id = cols[0].getIntValue();
            m.name = cols[1].unquoted();
            juce::String tcStr = cols[2].unquoted();
            SMPTETimecode tc = SMPTETimecode::parse(tcStr, smpteRate);
            m.timeSeconds = tc.toSeconds(smpteRate);
            if (cols.size() >= 6)
            {
                m.bar = cols[4].getIntValue();
                m.beat = cols[5].getDoubleValue();
            }
            newMarkers.push_back(m);
        }
    }

    if (!newMarkers.empty())
    {
        std::lock_guard<std::mutex> lock(markersMutex_);
        markers_ = std::move(newMarkers);
        sortMarkersInternal();
        notifyListeners();
        return true;
    }
    return false;
}

} // namespace MScoreVideoSync
