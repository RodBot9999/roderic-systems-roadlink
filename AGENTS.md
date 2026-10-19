# RoadLink contributor rules

- Preserve every existing user-facing feature and connectivity option.
- Before deleting, hiding, disabling, or replacing any user-facing behavior,
  obtain explicit approval from the user. Do not infer approval from a refactor,
  cleanup, memory optimization, dependency change, or architecture rewrite.
- Internal code may be replaced when the externally visible behavior remains.
- Automatic router mapping and temporary tunnel access are independent features;
  changes to one must not remove or silently disable the other.
