"""Item ids used by the Chao Garden animal array (SA2B). Animals 0-20, Chaos Drives 21-24."""
ANIMALS = {0: "penguin", 1: "seal", 2: "otter", 3: "rabbit", 4: "cheetah", 5: "boar", 6: "bear", 7: "tiger",
           8: "gorilla", 9: "peacock", 10: "parrot", 11: "condor", 12: "skunk", 13: "sheep", 14: "raccoon",
           15: "half_fish", 16: "skeleton_dog", 17: "bat", 18: "dragon", 19: "unicorn", 20: "phoenix",
           21: "yellow_drive", 22: "green_drive", 23: "red_drive", 24: "purple_drive"}
BY_NAME = {v: k for k, v in ANIMALS.items()}


def parse_type(text):
    """Accept an id ('23') or a name ('red_drive'); raise ValueError otherwise."""
    t = text.lower()
    if t.isdigit() and int(t) in ANIMALS:
        return int(t)
    if t in BY_NAME:
        return BY_NAME[t]
    raise ValueError(f"Unknown item '{text}'. Valid: " + ", ".join(f"{n}={k}" for k, n in ANIMALS.items()))
