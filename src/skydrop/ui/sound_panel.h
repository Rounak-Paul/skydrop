#pragma once

class SoundPanel {
public:
    static void OnUI();

private:
    // ---- Spatial state -------------------------------------------------------
    static int   _spatialPreset;   // 0=Off 1=Room 2=ConcertHall 3=OpenAir
    static float _spatialAzimuth;  // degrees

    // ---- EQ state (dB, displayed/stored; converted to linear on apply) -------
    static float _bassDb;     // −12 … +12
    static float _midDb;
    static float _trebleDb;
};
