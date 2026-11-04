# Changelog

## v1.1.0

- Dodano przewijane, wirtualizowane przeglądarki IR w CAB A i CAB B: ścieżka folderu, BROWSE, wybór wiersza, strzałki klawiatury oraz Previous/Next z zawijaniem.
- Dodano naturalne sortowanie WAV/AIF/AIFF i skanowanie folderów w tle.
- Drag & drop aktualizuje folder, listę i zaznaczenie odpowiedniego CAB.
- Dodano domyślnie włączone Link IR Browser Folders; foldery synchronizują się w obie strony bez podmiany aktywnego IR drugiego CAB.
- Stan projektu przechowuje folderA, folderB, activeIRA, activeIRB, linkFolders oraz ostatnio zmieniony browser. Osadzone IR nadal działają po usunięciu źródłowych plików.
- Obniżono końcowy gain o dodatkowe 12 dB w istniejącym stopniu OUTPUT, przed miernikiem; nie zmieniono parametrów ani działania bypass.
- Zachowano nazwę RottingIR, RottingPitStudio, logo, tło, identyfikatory VST3 i zgodność odczytu starszego stanu.
- Dodano testy przeglądarek, GUI, recall, identyfikatora VST3 i kalibracji wyjścia.

## v1.0.0

- Pierwsze stabilne wydanie RottingIR od RottingPitStudio.
- Dwa niezależne sloty IR WAV/AIFF, miks A/B, poziomy, polaryzacja i bypass.
- Asynchroniczne ładowanie i osadzanie obu IR w stanie projektu.
- Skalowalne GUI z tłem Rotting Pit Studio.
- Wprowadzono SemVer, archiwalne tagi i automatyczne wydania Windows x64 po testach.

Wydanie bazuje na kodzie b99357b63e0d36e56608308b0836b9765f0288ce;
zmiany dotyczą numeru wersji i infrastruktury wydań, bez zmian DSP ani parametrów.
