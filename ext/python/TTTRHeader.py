
@property
def json(self):
    return self.get_json()

@json.setter
def json(self, v):
    return self.set_json(v)

@property
def tags(self):
    import json
    return json.loads(self.json)["tags"]

def tag(self, name, idx=-1):
    import json
    js = self.get_json(name, idx, 0)
    return json.loads(js)

def add_tags(self, header2):
    import json
    if isinstance(header2, str):
        header2_dict = json.loads(header2)
    elif isinstance(header2, TTTRHeader):
        header2_dict = json.loads(header2.json)
    elif isinstance(header2, dict):
        header2_dict = header2
    # Copy tags from header 2 to header 1 if the tag is not in header 1
    header1_dict = json.loads(self.json)
    # remove Header_End tags
    tags_1 = header1_dict["tags"]
    tags_2 = header2_dict["tags"]
    for tag2 in tags_2:
        existing_tag = False
        for tag1 in tags_1:
            if (tag1["name"] == tag2["name"]) and (tag1["idx"] == tag2["idx"]):
                existing_tag = True
                break
        if not existing_tag:
            tags_1.append(tag2)
    header1_dict["tags"] = tags_1
    self.set_json(json.dumps(header1_dict))

def set_tag(self, name, value, value_type, idx=-1):
    """
    Set the tag with given name and value.
    If idx is provided (not -1), overwrite any existing tag matching name+idx;
    otherwise operate on the tag with name and idx=-1.
    """
    import json

    # 1) Load current header dict
    header = json.loads(self.json)
    tags = header.get("tags", [])

    # 2) Look for an existing tag and overwrite it
    for i, tag in enumerate(tags):
        if tag.get("name") == name and tag.get("idx", -1) == idx:
            tags[i] = {"name": name, "idx": idx, "value": value, "type": value_type}
            break
    else:
        # not found → append new
        tags.append({"name": name, "idx": idx, "value": value, "type": value_type})

    # 3) Write back via set_json
    header["tags"] = tags
    self.set_json(json.dumps(header))

def to_csv(self, filename=None):
    """
    Export the TTTR header tags as a CSV table.
    
    Parameters
    ----------
    filename : str, optional
        If provided, write the CSV to this file. If None, return as string.
    
    Returns
    -------
    str or None
        If filename is None, returns the CSV string. Otherwise, writes to file and returns None.
    
    Examples
    --------
    >>> header = tttrlib.TTTRHeader()
    >>> # Export to file
    >>> header.to_csv('header.csv')
    >>> # Get as string
    >>> csv_string = header.to_csv()
    """
    import csv
    import io
    import json
    
    # Get tags from header
    tags = self.tags
    
    # Create CSV in memory
    output = io.StringIO()
    writer = csv.writer(output)
    
    # Write header row
    writer.writerow(['name', 'idx', 'value', 'type'])
    
    # Write data rows
    for tag in tags:
        name = tag.get('name', '')
        idx = tag.get('idx', -1)
        value = tag.get('value', '')
        value_type = tag.get('type', '')
        writer.writerow([name, idx, value, value_type])
    
    csv_string = output.getvalue()
    
    # If filename provided, write to file
    if filename is not None:
        with open(filename, 'w', newline='') as f:
            f.write(csv_string)
        return None
    
    return csv_string

def __init__(self, *args, **kwargs):
    import os

    if len(args) > 0 and isinstance(args[0], os.PathLike):
        args = (os.fspath(args[0]),) + tuple(args[1:])

    this = _tttrlib.new_TTTRHeader(*args, **kwargs)
    try:
        self.this.append(this)
    except:
        self.this = this

@staticmethod
def write_ptu_header(fn, header, modes="wb"):
    import os
    if isinstance(fn, os.PathLike):
        fn = os.fspath(fn)
    return _tttrlib.TTTRHeader_write_ptu_header(fn, header, modes)

@staticmethod
def write_ht3_header(fn, header, modes="wb"):
    import os
    if isinstance(fn, os.PathLike):
        fn = os.fspath(fn)
    return _tttrlib.TTTRHeader_write_ht3_header(fn, header, modes)

@staticmethod
def write_spc132_header(fn, header, modes="wb"):
    import os
    if isinstance(fn, os.PathLike):
        fn = os.fspath(fn)
    return _tttrlib.TTTRHeader_write_spc132_header(fn, header, modes)

