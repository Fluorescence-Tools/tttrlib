"""
Test script to demonstrate CLSMImage initialization with filename parameter.

This script shows all supported ways to initialize a CLSMImage:
1. Filename string as positional argument (NEW - most convenient)
2. Path object as positional argument (NEW)
3. TTTR object as positional argument (EXISTING)
4. Filename as keyword argument (NEW)
5. TTTR object as keyword argument (EXISTING)
"""

import tttrlib
from pathlib import Path

# Example 1: Initialize CLSMImage with filename as positional argument (RECOMMENDED)
print("Example 1: Initialize with filename as positional argument")
try:
    # Replace with actual file path
    img = tttrlib.CLSMImage("path/to/your/file.ptu")
    print(f"  CLSMImage created successfully")
    print(f"  TTTR object accessible via: img.tttr_data")
    print(f"  Image shape: {img.shape}")
    print(f"  TTTR filename: {img.tttr_data.get_filename()}")
except Exception as e:
    print(f"  Error: {e}")

print()

# Example 2: Initialize CLSMImage with Path object as positional argument
print("Example 2: Initialize with Path object as positional argument")
try:
    # Replace with actual file path
    img = tttrlib.CLSMImage(Path("path/to/your/file.ptu"))
    print(f"  CLSMImage created successfully")
    print(f"  TTTR object accessible via: img.tttr_data")
    print(f"  Image shape: {img.shape}")
    print(f"  TTTR filename: {img.tttr_data.get_filename()}")
except Exception as e:
    print(f"  Error: {e}")

print()

# Example 3: Initialize CLSMImage with TTTR object as positional argument
print("Example 3: Initialize with TTTR object as positional argument")
try:
    # Replace with actual file path
    tttr = tttrlib.TTTR("path/to/your/file.ptu")
    img = tttrlib.CLSMImage(tttr)  # Pass TTTR as positional argument
    print(f"  CLSMImage created successfully")
    print(f"  TTTR object accessible via: img.tttr_data")
    print(f"  Image shape: {img.shape}")
except Exception as e:
    print(f"  Error: {e}")

print()

# Example 4: Initialize CLSMImage with filename as keyword argument
print("Example 4: Initialize with filename as keyword argument")
try:
    # Replace with actual file path
    img = tttrlib.CLSMImage(filename="path/to/your/file.ptu")
    print(f"  CLSMImage created successfully")
    print(f"  TTTR object accessible via: img.tttr_data")
    print(f"  Image shape: {img.shape}")
    print(f"  TTTR filename: {img.tttr_data.get_filename()}")
except Exception as e:
    print(f"  Error: {e}")

print()

# Example 5: Initialize CLSMImage with TTTR object as keyword argument
print("Example 5: Initialize with TTTR object as keyword argument")
try:
    # Replace with actual file path
    tttr = tttrlib.TTTR("path/to/your/file.ptu")
    img = tttrlib.CLSMImage(tttr_data=tttr)  # Pass TTTR as keyword argument
    print(f"  CLSMImage created successfully")
    print(f"  TTTR object accessible via: img.tttr_data")
    print(f"  Image shape: {img.shape}")
except Exception as e:
    print(f"  Error: {e}")

print()

# Example 6: Access TTTR object from CLSMImage
print("Example 6: Access TTTR object from CLSMImage")
try:
    # Both methods now store the TTTR object as an attribute
    # You can access it via img.tttr_data
    # This is in addition to the C++ method img.get_tttr()
    
    # Replace with actual file path
    img = tttrlib.CLSMImage(filename="path/to/your/file.ptu")
    
    # Access via Python attribute
    tttr_via_attr = img.tttr_data
    print(f"  Via attribute: {tttr_via_attr}")
    
    # Access via C++ getter method
    tttr_via_getter = img.get_tttr()
    print(f"  Via getter: {tttr_via_getter}")
    
    # Both should reference the same object
    print(f"  Same object: {tttr_via_attr is tttr_via_getter}")
    
except Exception as e:
    print(f"  Error: {e}")
