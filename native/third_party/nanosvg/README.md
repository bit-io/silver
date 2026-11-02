# NanoSVG

Biblioteka `nanosvg.h` + `nanosvgrast.h` (Mikko Mononen, licencja zlib) — rasteryzer SVG używany przez `silver_draw_svg`.
Źródło: https://github.com/memononen/nanosvg (katalog `src/`), pliki **bez modyfikacji**.
Obsługuje: path, rect, circle, ellipse, line, polyline, polygon, g, transformacje, fill/stroke (kolor, gradienty liniowe i radialne),
opacity, stroke-width/linecap/linejoin/miterlimit/dasharray/dashoffset, fill-rule, viewBox.
Nie obsługuje: text, markery, clip-path, filtry, mask, use (Silver rozwija `use`, markery i tekst w serializerze H#).
