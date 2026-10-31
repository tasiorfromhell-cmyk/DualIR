# DualIR

DualIR to wtyczka VST3 C++20/JUCE z dwoma równoległymi silnikami konwolucji do odpowiedzi impulsowych kolumn gitarowych. Obsługuje mono i stereo, automatyzację parametrów oraz zapis IR wewnątrz stanu projektu DAW.

## Obsługa

- Wczytaj WAV/AIFF przyciskiem **Load IR** w slocie A lub B albo przeciągnij go na wybrany slot. Dwa upuszczone pliki trafiają kolejno do A i B.
- Każdy slot ma poziom od −24 do +24 dB, włączenie/wyciszenie, odwrócenie polaryzacji i przycisk usunięcia IR.
- Przy dwóch załadowanych IR suwak **A/B blend** miesza ich wyjścia liniowo: A ma wagę `1 − blend`, B ma wagę `blend`. Wyciszenie slotu zachowuje wagę drugiego slotu.
- Jeden załadowany IR otrzymuje pełną wagę niezależnie od blend. Dwa puste sloty przepuszczają sygnał bez zmian; nadal działa poziom wyjściowy od −24 do +12 dB.
- **Bypass** przepuszcza oryginalny sygnał, omijając konwolucję i poziom wyjściowy. Wskaźniki pokazują poziomy wejścia i wyjścia.

IR może mieć jeden lub dwa kanały, częstotliwość 8–384 kHz, długość do 2 sekund i rozmiar pliku do 16 MiB. Loader odrzuca uszkodzone dane, ciszę i próbki NaN/Inf. Zachowuje amplitudę, początkową ciszę i długość IR, bez normalizacji ani przycinania. JUCE przelicza częstotliwość próbkowania do częstotliwości sesji.

Dekodowanie plików i przygotowanie nowych filtrów odbywa się w tle. Przetwarzanie audio ma zerową latencję algorytmiczną; zmiany poziomów, miksu i bypassu są wygładzane przez 20 ms, a JUCE płynnie przełącza filtry po zakończeniu ich przygotowania. Nieudane wczytanie pokazuje błąd i zachowuje poprzedni działający IR.

Stan wtyczki zawiera parametry i oba IR w bezstratnym formacie float32 WAV. Po zapisaniu projektu lub presetu oryginalne ścieżki plików nie są potrzebne do odtworzenia brzmienia. Edytor otwiera się w rozmiarze 800 × 500 i skaluje w zakresie 640 × 400–1600 × 1000.

## Windows x64 — Visual Studio 2022

Wymagania: Visual Studio 2022 z pakietem **Programowanie aplikacji klasycznych w języku C++**, MSVC v143, Windows SDK oraz CMake 3.25+ w `PATH`. Pierwsza konfiguracja pobiera JUCE przez HTTPS z `codeload.github.com`.

W katalogu repozytorium, w PowerShell lub terminalu deweloperskim Visual Studio:

```powershell
cmake --preset windows-vs2022
cmake --build --preset windows-release --parallel 2
ctest --preset windows-release
```

Preset wybiera generator **Visual Studio 17 2022** i architekturę **x64**. Wariant Debug:

```powershell
cmake --build --preset windows-debug --parallel 2
ctest --preset windows-debug
```

Pakiet Release:

```text
build/windows-vs2022/DualIR_artefacts/Release/VST3/DualIR.vst3/
```

Kopiuj cały katalog `DualIR.vst3`, wraz z `Contents` i pozostałą zawartością. Build nie instaluje automatycznie wtyczki w katalogach systemowych. W Visual Studio można też otworzyć wygenerowany `build/windows-vs2022/DualIR.sln`.

## Sprawdzenie w REAPER

1. Zamknij REAPER i skopiuj cały pakiet `DualIR.vst3` do `%LOCALAPPDATA%\Programs\Common\VST3` lub własnego katalogu VST3.
2. W **Preferences → Plug-ins → VST** dodaj użyty katalog do ścieżek skanowania i wykonaj ponowne skanowanie.
3. Dodaj **VST3: DualIR** do ścieżki. Bez IR sprawdź przepuszczanie sygnału; następnie wczytaj IR do A i B i sprawdź zmianę brzmienia, blend, gain, mute, polaryzację i bypass.
4. Sprawdź otwieranie i skalowanie edytora oraz pracę z sygnałem mono i stereo.
5. Zapisz projekt, zamknij go i otwórz ponownie. Sprawdź odtworzenie parametrów i IR także po przeniesieniu oryginalnych plików.

## Linux i środowisko chmurowe

Preset `linux-debug` wymaga kompilatora C++20, CMake 3.25+, Ninja, `pkg-config` i bibliotek deweloperskich ALSA, FreeType, Fontconfig, X11, Xext, Xinerama, Xrandr, Xcursor i Xcomposite. Po przygotowaniu zależności:

```sh
cmake --preset linux-debug
cmake --build --preset linux-debug
ctest --preset linux-debug
```

Wersja zoptymalizowana używa osobnego katalogu budowania:

```sh
cmake --preset linux-release
cmake --build --preset linux-release
ctest --preset linux-release
```

Na Debianie 13 skrypt odtwarza pełny build i testy bez uprawnień administratora:

```sh
bash scripts/setup-cloud.sh
```

Instaluje CMake 3.31.6 i Ninja 1.11.1.4 w `/workspace/dualir-tools`, a zweryfikowane pakiety Debian w `/workspace/dualir-deps`. Wymaga dostępu do PyPI, `files.pythonhosted.org`, `deb.debian.org` i `codeload.github.com` oraz możliwości uruchomienia lokalnego Xvfb. OpenGL i WebKit nie są wymagane.

Przed kolejną kompilacją w nowym terminalu:

```sh
export PATH=/workspace/dualir-tools/bin:$PATH
source /workspace/dualir-deps/env.sh
cd /workspace/DualIR
cmake --preset linux-debug
cmake --build --preset linux-debug
ctest --preset linux-debug
```

Ścieżka pakietu Debug w tym środowisku:

```text
/workspace/DualIR/build/linux-debug/DualIR_artefacts/Debug/VST3/DualIR.vst3
```

Ścieżka pakietu Release:

```text
/workspace/DualIR/build/linux-release/DualIR_artefacts/Release/VST3/DualIR.vst3
```

To pakiet dla Linuksa. Nie jest binarną wtyczką Windows.

## Walidacja i CI

| Element | Status |
| --- | --- |
| Linux x64: konfiguracja CMake i build VST3 Debug oraz Release | VERIFIED |
| Pełny CTest: 6/6 zestawów w Debug i Release | VERIFIED |
| DSP, loader IR i integracja procesora | VERIFIED |
| Ładowanie natywnego VST3 i audio mono/stereo w niezależnym test-hoście | VERIFIED |
| Kompilacja Windows x64 / MSVC | NOT VERIFIED |
| Wykonanie workflow Windows CI | NOT VERIFIED |
| Skanowanie, interfejs i odtwarzanie projektu w REAPER | NOT VERIFIED |

CTest obejmuje:

- `DualIR.DSP`: FIR, stereo, resampling, miks, gain, polaryzacja, mute, bypass i duże bloki audio.
- `DualIR.IRLoader`: WAV/AIFF, bezstratny zapis i walidacja błędnych danych.
- `DualIR.PluginIntegration`: rzeczywiste IR w procesorze, automatyzacja, stan z osadzonymi IR, brak oryginalnych plików i wymiana filtra podczas odtwarzania.
- `DualIR.VST3Host`: ładowanie wygenerowanej biblioteki VST3, interfejsy SDK i przetwarzanie buforów hosta.
- `DualIR.ProcessorSmoke`: konfiguracje kanałów, częstotliwości i rozmiary bloków, fabryka procesora i metadane.
- `DualIR.EditorSmoke`: tworzenie, skalowanie i renderowanie edytora. Na Linuksie test jest rejestrowany, gdy CMake znajdzie `xvfb-run`.

Test edytora sprawdza także powiązania kontrolek z parametrami, drop rzeczywistych plików WAV i usuwanie IR. Test integracji obejmuje poprawne ustawienia mute, gain i bypass od pierwszej próbki po przygotowaniu oraz przywołaniu stanu.

Workflow **Windows x64 VST3** (`.github/workflows/windows.yml`) działa na `windows-latest`, pobiera pełną historię repozytorium i jawnie wybiera Visual Studio 2022 oraz toolset v143. Gdy obraz runnera nie ma VS2022, instaluje oficjalne Build Tools po sprawdzeniu podpisu Microsoft. Buduje Release, uruchamia cały CTest i sprawdza nagłówki PE/AMD64 wygenerowanej biblioteki. ZIP obejmuje cały katalog `DualIR.vst3`, także pliki ukryte. W razie błędu zachowuje dostępne logi CMake/CTest. Sam plik workflow ani walidacja actionlint nie potwierdzają wykonania Windows build.

### Uruchomienie i pobranie Windows build w GitHub

Kod projektu, w tym `.github/workflows/windows.yml`, musi znajdować się w repozytorium GitHub. Pliki istniejące tylko w środowisku chmurowym nie są dostępne dla Actions.

1. Otwórz repozytorium `tasiorfromhell-cmyk/DualIR` w GitHub i wybierz **Actions**. Jeśli GitHub wyświetli prośbę o włączenie Actions, włącz je.
2. Po lewej wybierz **Windows x64 VST3**, następnie **Run workflow**, gałąź zawierającą projekt (np. `main`) i zielony przycisk **Run workflow**. Workflow uruchamia się też automatycznie przy pushu.
3. Otwórz nowy przebieg i poczekaj na zielony wynik zadania **VS2022 x64 Release**. Przy błędzie otwórz nieudany krok, aby przeczytać log.
4. Na stronie pomyślnego przebiegu przewiń do **Artifacts** i kliknij **DualIR-Windows-x64-VST3**. Pobieranie wymaga zalogowania do GitHub.
5. Rozpakuj pobrany ZIP artefaktu, a następnie zawarty w nim `DualIR-Windows-x64-VST3.zip`. Wewnętrzny ZIP zawiera katalog `DualIR.vst3/` z binarną wtyczką `DualIR.vst3/Contents/x86_64-win/DualIR.vst3`.
6. Do REAPER zainstaluj cały katalog `DualIR.vst3`, nie samą wewnętrzną bibliotekę. Instrukcja skanowania znajduje się wyżej.

## Struktura i zależności

```text
Source/PluginProcessor.h/.cpp    # audio, parametry, wątek loadera i stan
Source/PluginEditor.h/.cpp       # skalowalne okno wtyczki
Source/DSP/CabProcessor.h/.cpp   # pojedyncza konwolucja
Source/DSP/DualCabEngine.h/.cpp  # równoległy silnik A/B
Source/IR/IRLoader.h/.cpp        # dekodowanie i walidacja IR
Source/GUI/MainPanel.h/.cpp      # kontrolki i obsługa plików
Tests/                         # testy DSP, integracji, GUI i VST3
scripts/                       # przygotowanie środowiska Linux
CMakePresets.json              # VS2022 x64 i Linux/Ninja
.github/workflows/windows.yml  # build, testy i ZIP Windows
```

CMake pobiera JUCE 8.0.12 z oficjalnego repozytorium, przypięte do commitu `29396c22c93392d6738e021b83196283d6e4d850`. Archiwum jest sprawdzane sumą SHA-256 `97cc01e0fe15278f6edadc895a3cf0aac2fd98bcd023823384d5ca7025beb91c`, z włączoną weryfikacją TLS. SDK VST3 jest dołączone do JUCE; Projucer ani oddzielna instalacja SDK nie są potrzebne.

JUCE jest dostępne na licencji AGPLv3 lub komercyjnej: [warunki JUCE](https://juce.com/legal/juce-8-licence/). Dodanie JUCE nie ustala automatycznie licencji pozostałego kodu DualIR.
