# Test Data Setup

## Overview

tttrlib tests require test data files to run. You can:
1. **Automatically download** required files using `test/download_test_data.py`
2. **Manually specify** the location using the `TTTRLIB_DATA` environment variable

## Quick Start

### Automatic Download (Recommended)

```bash
# Download all required test data
python test/download_test_data.py

# Or specify output directory
python test/download_test_data.py --output-dir /path/to/tttr-data

# Then run tests
pytest test/
```

### Manual Setup

If you already have test data elsewhere, set the environment variable (see below).

## Setting Test Data Location

### Option 1: Environment Variable (Recommended)

Set the `TTTRLIB_DATA` environment variable to point to your test data directory:

#### Linux/macOS
```bash
export TTTRLIB_DATA=/path/to/tttr-data
pytest test/
```

#### Windows (PowerShell)
```powershell
$env:TTTRLIB_DATA = "V:\tttr-data"
pytest test/
```

#### Windows (Command Prompt)
```cmd
set TTTRLIB_DATA=V:\tttr-data
pytest test/
```

### Option 2: settings.json

Edit `test/settings.json` and set the `data_root` field:

```json
{
  "data_root": "/path/to/tttr-data",
  "spc132_filename": "imaging/spc/spc132_file.spc",
  ...
}
```

## Priority Order

The test framework looks for test data in this order:

1. **TTTRLIB_DATA** environment variable (if set)
2. **data_root** from `settings.json`
3. **./tttr-data** (relative to repository root)

## Test Data Structure

Expected directory structure:

```
tttr-data/
├── imaging/
│   ├── spc/
│   │   └── spc132_file.spc
│   ├── zeiss/
│   │   └── eGFP_bad_background/
│   │       └── eGFP_bad_background.ptu
│   └── aberior/
│       └── pq-mfd-sted/
│           └── DA_exc561nm15prozent_STED775_100prozent_01.ptu
└── ...
```

## Running Tests

### Run all tests
```bash
pytest test/
```

### Run specific test file
```bash
pytest test/test_TTTR.py -v
```

### Run with custom data location
```bash
TTTRLIB_DATA=/custom/path pytest test/test_swig_coverage.py -v
```

### Check test data availability
```bash
pytest test/ --collect-only
```

The test output will show:
```
✓ Test data root: /path/to/tttr-data
```

Or if data is not found:
```
✗ Test data root: /path/to/tttr-data
  WARNING: Test data directory not found!
```

## Debugging

To see which data files are being used:

```bash
# Run with verbose output
pytest test/ -vv

# Run with print statements
pytest test/ -s
```

The `conftest.py` file in the test directory will print the resolved data root path at the start of the test run.

## Environment Variable Examples

### Using a network share (Windows)
```powershell
$env:TTTRLIB_DATA = "\\server\share\tttr-data"
pytest test/
```

### Using a local directory (Linux)
```bash
export TTTRLIB_DATA=$HOME/data/tttr-data
pytest test/
```

### Temporary for single test run
```bash
TTTRLIB_DATA=/tmp/test-data pytest test/test_TTTR.py
```

## Troubleshooting

### Tests are skipped with "Data directory not found"

1. Check that `TTTRLIB_DATA` is set correctly:
   ```bash
   echo $TTTRLIB_DATA  # Linux/macOS
   echo %TTTRLIB_DATA%  # Windows cmd
   $env:TTTRLIB_DATA   # Windows PowerShell
   ```

2. Verify the directory exists and contains test data:
   ```bash
   ls $TTTRLIB_DATA
   ```

3. Check `settings.json` has correct `data_root`:
   ```bash
   cat test/settings.json | grep data_root
   ```

### Wrong data files are being used

1. Check priority order - environment variable takes precedence
2. Verify `TTTRLIB_DATA` is not set if you want to use `settings.json`:
   ```bash
   unset TTTRLIB_DATA  # Linux/macOS
   ```

## CI/CD Integration

For continuous integration, set the environment variable in your CI configuration:

### GitHub Actions
```yaml
env:
  TTTRLIB_DATA: /path/to/test/data
```

### GitLab CI
```yaml
variables:
  TTTRLIB_DATA: /path/to/test/data
```

### Jenkins
```groovy
environment {
    TTTRLIB_DATA = '/path/to/test/data'
}
```
