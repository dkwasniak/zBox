# 3D Printing Tips

Practical print orientation and support notes for the zBox enclosure parts.
Part names follow the CAD/STL naming used in this repository.

## Part Settings

| Part | Print orientation | Supports | Notes |
| --- | --- | --- | --- |
| `front_lid` | Any convenient orientation | No | The part should print cleanly without support material. |
| `nfc_card_slot` | Place it on the bottom/base face | Usually no | Keep the base flat on the build plate for the cleanest fit. |
| `rear_lid` | Place it on the inner face | Yes | Supports are needed around the ESP cutout. |
| `shell` | Place it on the front face | Yes | The front face is the side closer to the button cutout. |
| `speaker_holders` | Place each holder on its side, using the largest flat face | Usually no | This gives the part the most stable contact with the build plate. |
| `wall` | Place it on the bottom/base face | Usually no | Use the flat base as the build-plate contact surface. |

## General Notes

- Preview generated supports in the slicer before printing.
- Keep visible exterior faces away from support contact whenever possible.
- If the fit is tight, test-fit the lids and NFC card slot before final assembly.
