from dataclasses import dataclass


@dataclass(frozen=True)
class AeroParams:
    p1: float = 0.06995
    p2: float = 1.02337
    p3: float = -0.76057
    p4: float = 0.16971
    p5: float = 0.46585
    p6: float = 0.28214
    p7: float = 0.00000


CALIBRATED = AeroParams()
