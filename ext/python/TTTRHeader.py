
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


def __init__(self, *args, **kwargs):
    this = _tttrlib.new_TTTRHeader(*args, **kwargs)
    try:
        self.this.append(this)
    except:
        self.this = this

