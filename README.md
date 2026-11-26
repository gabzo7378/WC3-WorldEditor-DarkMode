# WC3-WorldEditor-DarkMode

Modo **UltraDark 100% portátil y exclusivo por proceso** para el **World Editor clásico de Warcraft III** (`worldedit.exe`), sin modificar el tema de Windows ni alterar ningún archivo original del juego.

---

## Características principales

- **Aislado por proceso:** Solo aplica el tema oscuro a `worldedit.exe` cuando lo abres desde `WorldEditor_Dark.exe`. Tu Windows y el resto de tus programas no se tocan.
- **Convivencia con el editor normal:** Conservas tu `worldedit.exe` intacto (modo claro original) y tienes `WorldEditor_Dark.exe` al lado para abrirlo en modo oscuro cuando quieras.
- **100% Portátil y ordenado:** Cero dependencias externas ni instaladores. Solo se copia un `.exe` y la subcarpeta `WE_DarkMode\` dentro del directorio de Warcraft III en cualquier PC.
- **Cobertura visual completa:**
  - Barras de título oscuras nativas (DWM) en ventana principal, Object Editor, Trigger Editor, Paletas y diálogos.
  - Barra de menú superior y menús desplegables oscuros (alineados a la izquierda con atajos a la derecha, separadores y marcas de verificación).
  - Tablas y árboles (`SysListView32` / `SysTreeView32`) con selección precisa y realce automático de textos morados/azules/verdes para lectura nítida.
  - Botones clásicos, casillas de verificación, selectores `◄ ►` y barra de estado inferior oscuros.
  - Renderizado inteligente de íconos en la barra de herramientas (mantiene los colores vivos en íconos activos y muestra íconos atenuados claros —nunca manchas negras— cuando están deshabilitados).
  - **No interfiere** con el lienzo 3D Direct3D / OpenGL del terreno, minimapa ni vista previa de modelos.

---

## Versiones disponibles en este repositorio

| Carpeta | Versiones de Warcraft III | Arquitectura | Estado |
| :--- | :--- | :--- | :--- |
| [`WC3-Classic-1.21-1.27b/`](./WC3-Classic-1.21-1.27b) | **1.21 – 1.27b** (`worldedit.exe` / `worldedit121.exe`) | Win32 (`x86`) | **Listo y estable** |

*(Próximamente se añadirán carpetas dedicadas para otras versiones del editor en este mismo repositorio).*

---

## Instalación rápida (Portátil)

1. Entra a la carpeta [`WC3-Classic-1.21-1.27b`](./WC3-Classic-1.21-1.27b).
2. Copia `WorldEditor_Dark.exe` y la carpeta `WE_DarkMode` dentro de la carpeta raíz de tu **Warcraft III**:

```text
<Carpeta de Warcraft III>\
├── worldedit.exe              <-- Editor clásico normal (intacto)
├── WorldEditor_Dark.exe       <-- Ábrelo para usar el Modo Oscuro
└── WE_DarkMode\
    ├── WE_DarkMode.dll        <-- Motor UltraDark (32-bit, sin dependencias)
    └── WE_DarkMode.ini        <-- Paleta de colores editable
```

3. Ejecuta **`WorldEditor_Dark.exe`** (también puedes arrastrar un mapa `.w3x` / `.w3m` directamente encima de `WorldEditor_Dark.exe` para abrirlo en modo oscuro).

---

## Personalización de colores (`WE_DarkMode.ini`)

Puedes ajustar todos los colores en formato hexadecimal `RRGGBB` editando `WE_DarkMode\WE_DarkMode.ini`:

```ini
[General]
; Vacío = detecta automáticamente worldedit.exe junto a WorldEditor_Dark.exe
WorldEditPath=
Arguments=

[Colors]
BgColor=181818
SurfaceColor=1E1E1E
InputColor=121212
TextColor=EAEAEA
MutedTextColor=8A8A8A
AccentColor=264F78
HoverColor=2E2E32
BorderColor=383838
```

---

## Compilar desde el código fuente

Requisitos: **MSYS2 MinGW-w64 32-bit** (`mingw-w64-i686-gcc` en `C:\msys64\mingw32\bin\g++.exe`).

```powershell
.\build.ps1
```

Genera binarios estáticos (`WE_DarkMode.dll` y `WorldEditor_Dark.exe`) enlazados únicamente contra DLLs nativas del sistema operativo Windows.

---

## Licencia

Distribuido bajo la licencia [MIT](./LICENSE).
