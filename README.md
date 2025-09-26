# SaladDashboard
A C++ dashboard for monitoring Salad API data using WebView2.

## Dependencies
- Visual Studio 2022+ (Community edition)
- NuGet packages: Microsoft.Web.WebView2, Microsoft.Windows.ImplementationLibrary (WIL)
- Microsoft Edge WebView2 Runtime (install if prompted)

## Build Instructions
1. Clone this repo: `git clone https://github.com/CursedAtom/Salad-Dashboard-cpp.git`
2. Open in Visual Studio.
3. Set configuration to Release, platform to x64, and language standard to C++20.
4. Build Solution (Ctrl+Shift+B).
5. Place a valid Salad API token in `C:\ProgramData\Salad\token.txt` for full functionality. This will already exist if you have Salad installed.

## Usage
Run the generated `SaladDashboard.exe` in the Release folder.

## Notes
Unsigned exe may trigger Windows Defender false positives—build locally to avoid issues.
