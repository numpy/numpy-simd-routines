from .datatypes import SRType


def bitcast(eltype, value):
    tp = SRType.byid(value.container_id, eltype.element_id)
    return tp.from_bytes(value.to_bytearray())
