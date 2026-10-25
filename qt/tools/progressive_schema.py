"""Exact scalar encoding for the draft schema-2 metadata contract.

The player validates the full snapshot via ProgressiveClient.validate_session().
These helpers deliberately reject floats/bools instead of rounding them.
"""
def unsigned64(value: int, *, byte_offset: bool = False, positive: bool = False) -> str:
    maximum = 2**63 - 1 if byte_offset else 2**64 - 1
    if type(value) is not int or not (1 if positive else 0) <= value <= maximum:
        raise ValueError('Expected an exact unsigned integer in the schema range')
    return str(value)


def signed64(value: int) -> str:
    if type(value) is not int or not -2**63 <= value < 2**63:
        raise ValueError('Expected an exact signed 64-bit integer')
    return str(value)
