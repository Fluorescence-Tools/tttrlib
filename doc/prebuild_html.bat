@echo off
pushd %~dp0
echo [clean_html] Removing _build\html
if exist _build\html rmdir /s /q _build\html
popd

rem --- Pre-run examples to update sphinx-gallery blacklist ---
set "EXAMPLES_DIR=..\examples"
if not defined EXAMPLES_TIMEOUT set "EXAMPLES_TIMEOUT=90"
set "TTTRLIB_DOCS=1"  rem let examples downscale work if they want

if exist "run_examples_blacklist.py" (
  echo [make_html] Pre-running examples to update blacklist...
  python -u "run_examples_blacklist.py" --examples "%EXAMPLES_DIR%" --blacklist "gallery_blacklist.txt" --timeout %EXAMPLES_TIMEOUT%
) else (
  echo [make_html] Skipping pre-run: run_examples_blacklist.py not found.
)
