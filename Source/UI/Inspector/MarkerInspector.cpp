#include "MarkerInspector.h"

namespace MScoreVideoSync
{

MarkerInspector::MarkerInspector(CueMarkerModel& markerModel, SMPTEFrameRate smpteRate)
    : markerModel_(markerModel), smpteRate_(smpteRate)
{
    markerModel_.addListener(this);

    // Setup Table Columns
    table_.setModel(this);
    table_.setColour(juce::ListBox::backgroundColourId, Vep8Theme::bgInput);
    table_.setOutlineThickness(1);
    table_.getHeader().addColumn("ID", 1, 40, 30, 60);
    table_.getHeader().addColumn("Name", 2, 140, 80, 220);
    table_.getHeader().addColumn("SMPTE", 3, 110, 80, 140);
    table_.getHeader().addColumn("Bar.Beat", 4, 80, 60, 100);
    table_.getHeader().addColumn("Streamer", 5, 70, 50, 90);
    table_.getHeader().addColumn("Punch", 6, 60, 40, 80);
    table_.getHeader().addColumn("2-Pop", 7, 50, 40, 70);

    addAndMakeVisible(table_);

    addAndMakeVisible(btnAddMarker_);
    btnAddMarker_.onClick = [this] {
        markerModel_.addQuickMarker(0.0, 0.0, 1, 1.0, "New Hit");
    };

    addAndMakeVisible(btnDeleteMarker_);
    btnDeleteMarker_.onClick = [this] {
        int selected = table_.getSelectedRow();
        if (selected >= 0 && selected < static_cast<int>(cachedMarkers_.size()))
        {
            markerModel_.removeMarker(cachedMarkers_[selected].id);
        }
    };

    addAndMakeVisible(btnImportMidi_);
    btnImportMidi_.onClick = [this] { openMidiImportDialog(); };

    addAndMakeVisible(btnExportCsv_);
    btnExportCsv_.onClick = [this] { openCsvExportDialog(); };

    addAndMakeVisible(btnImportCsv_);
    btnImportCsv_.onClick = [this] { openCsvImportDialog(); };

    markersChanged();
}

MarkerInspector::~MarkerInspector()
{
    markerModel_.removeListener(this);
}

void MarkerInspector::addListener(Listener* listener)
{
    listeners_.add(listener);
}

void MarkerInspector::removeListener(Listener* listener)
{
    listeners_.remove(listener);
}

void MarkerInspector::setSmpteFrameRate(SMPTEFrameRate rate)
{
    smpteRate_ = rate;
    table_.repaint();
}

void MarkerInspector::markersChanged()
{
    cachedMarkers_ = markerModel_.getMarkersSorted();
    table_.updateContent();
    table_.repaint();
}

int MarkerInspector::getNumRows()
{
    return static_cast<int>(cachedMarkers_.size());
}

void MarkerInspector::paintRowBackground(juce::Graphics& g, int rowNumber, int /*width*/, int /*height*/, bool rowIsSelected)
{
    if (rowIsSelected)
    {
        g.setColour(Vep8Theme::cyan.withAlpha(0.25f));
        g.fillAll();
    }
    else
    {
        g.setColour(rowNumber % 2 == 0 ? Vep8Theme::bgInput : Vep8Theme::bgPanel);
        g.fillAll();
    }
}

void MarkerInspector::paintCell(juce::Graphics& g, int rowNumber, int columnId, int width, int height, bool rowIsSelected)
{
    if (rowNumber < 0 || rowNumber >= static_cast<int>(cachedMarkers_.size()))
        return;

    const auto& m = cachedMarkers_[rowNumber];
    juce::Rectangle<int> area(4, 0, width - 8, height);

    g.setFont(Vep8Theme::getUiFont(12.0f, false));
    g.setColour(rowIsSelected ? Vep8Theme::cyan : Vep8Theme::textPrimary);

    switch (columnId)
    {
        case 1: // ID
            g.drawText(juce::String(m.id), area, juce::Justification::centredLeft);
            break;
        case 2: // Name
            g.drawText(m.name, area, juce::Justification::centredLeft);
            break;
        case 3: // SMPTE
        {
            SMPTETimecode tc = SMPTETimecode::fromSeconds(m.timeSeconds, smpteRate_);
            g.setFont(Vep8Theme::getMonospaceFont(11.5f, false));
            g.setColour(Vep8Theme::amber);
            g.drawText(tc.toString(false), area, juce::Justification::centredLeft);
            break;
        }
        case 4: // Bar.Beat
            g.setFont(Vep8Theme::getMonospaceFont(11.5f, false));
            g.drawText(juce::String::formatted("%d.%d", m.bar, static_cast<int>(m.beat)), area, juce::Justification::centredLeft);
            break;
        case 5: // Streamer
            g.setColour(m.streamerEnabled ? Vep8Theme::green : Vep8Theme::textDim);
            g.drawText(m.streamerEnabled ? "ON" : "OFF", area, juce::Justification::centredLeft);
            break;
        case 6: // Punch
            g.setColour(m.punchEnabled ? Vep8Theme::red : Vep8Theme::textDim);
            g.drawText(m.punchEnabled ? "ON" : "OFF", area, juce::Justification::centredLeft);
            break;
        case 7: // 2-Pop
            g.setColour(m.twoPopAudioEnabled ? Vep8Theme::amber : Vep8Theme::textDim);
            g.drawText(m.twoPopAudioEnabled ? "ON" : "OFF", area, juce::Justification::centredLeft);
            break;
    }
}

void MarkerInspector::cellDoubleClicked(int rowNumber, int /*columnId*/, const juce::MouseEvent&)
{
    if (rowNumber >= 0 && rowNumber < static_cast<int>(cachedMarkers_.size()))
    {
        double pts = cachedMarkers_[rowNumber].timeSeconds;
        listeners_.call([&](Listener& l) { l.locateToMarker(pts); });
    }
}

void MarkerInspector::openMidiImportDialog()
{
    fileChooser_ = std::make_unique<juce::FileChooser>("Import MIDI Markers & Tempo Map from MuseScore 4",
                                                        juce::File::getSpecialLocation(juce::File::userDocumentsDirectory),
                                                        "*.mid;*.midi");

    fileChooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [this](const juce::FileChooser& fc) {
                                  auto file = fc.getResult();
                                  if (file.existsAsFile())
                                  {
                                      MidiMarkerImporter::importFromMidiFile(file, markerModel_);
                                  }
                              });
}

void MarkerInspector::openCsvExportDialog()
{
    fileChooser_ = std::make_unique<juce::FileChooser>("Export Cue Sheet to CSV",
                                                        juce::File::getSpecialLocation(juce::File::userDocumentsDirectory),
                                                        "*.csv");

    fileChooser_->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                              [this](const juce::FileChooser& fc) {
                                  auto file = fc.getResult();
                                  if (file != juce::File())
                                  {
                                      juce::String csv = markerModel_.exportToCsv(smpteRate_);
                                      file.replaceWithText(csv);
                                  }
                              });
}

void MarkerInspector::openCsvImportDialog()
{
    fileChooser_ = std::make_unique<juce::FileChooser>("Import Cue Sheet CSV",
                                                        juce::File::getSpecialLocation(juce::File::userDocumentsDirectory),
                                                        "*.csv");

    fileChooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [this](const juce::FileChooser& fc) {
                                  auto file = fc.getResult();
                                  if (file.existsAsFile())
                                  {
                                      markerModel_.importFromCsv(file.loadFileAsString(), smpteRate_);
                                  }
                              });
}

void MarkerInspector::paint(juce::Graphics& g)
{
    g.fillAll(Vep8Theme::bgPanel);
    g.setColour(Vep8Theme::border);
    g.drawRect(getLocalBounds(), 1);
}

void MarkerInspector::resized()
{
    auto bounds = getLocalBounds().reduced(6);

    auto btnBar = bounds.removeFromBottom(28);
    const int bw = btnBar.getWidth() / 5;
    btnAddMarker_.setBounds(btnBar.removeFromLeft(bw).reduced(2));
    btnDeleteMarker_.setBounds(btnBar.removeFromLeft(bw).reduced(2));
    btnImportMidi_.setBounds(btnBar.removeFromLeft(bw).reduced(2));
    btnExportCsv_.setBounds(btnBar.removeFromLeft(bw).reduced(2));
    btnImportCsv_.setBounds(btnBar.reduced(2));

    table_.setBounds(bounds);
}

} // namespace MScoreVideoSync
