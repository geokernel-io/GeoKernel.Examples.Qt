# GeoKernel HelloMap

Bu klasor Windows x64 ve Linux x64 uzerinde tek basina derlenebilen bagimsiz bir
Qt/C++ ornegidir. Yerel GeoKernel kaynak koduna veya ozel erisim tokenina bagli
degildir.

## Bagimliliklar

Ortak gereksinimler:

- CMake 3.22 veya ustu (`--fresh` kullanmak icin 3.24 veya ustu)
- Ninja
- Qt 6 (Core, Gui, Widgets, OpenGL, OpenGLWidgets, Network, Sql ve Svg)
- Internet erisimi

Windows icin MSVC x64, Linux icin GCC veya Clang gerekir. Linux sisteminde ayrica
Qt 6 gelistirme paketleriyle GeoKernel SDK'nin kullandigi GDAL ve OpenSSL runtime
kutuphaneleri kurulu olmalidir.

Ubuntu 22.04 icin gerekli paketler:

```bash
sudo apt update
sudo apt install -y \
  build-essential ninja-build unzip \
  qt6-base-dev qt6-base-dev-tools libqt6svg6-dev libqt6opengl6-dev \
  libqt6sql6-sqlite libxkbcommon-dev libgl1-mesa-dev \
  libgdal-dev libssl-dev
```

Her CMake configure isleminde GitHub'daki en son GeoKernel release etiketi sorgulanir.
Uygun Windows veya Linux x64 SDK arsivi indirilir, SHA256 ile dogrulanir ve
`.geokernel-sdk` altinda surume gore onbellege alinir. Yeni bir release yayinlandiginda
sonraki configure otomatik olarak yeni surumu kullanir. Eski bir CMake cache'i SDK
surumunu sabitleyemez ve token gerekmez.

Ornek veri uygulama ilk calistirildiginda public `GeoKernel.SampleData` release
alanindan indirilir. Dosyalar calistirilabilir dosyanin yanindaki
`data/world_4326` klasorunde tutulur ve sonraki calistirmalarda buradan kontrol edilir.

## Windows CMD ile derleme

Visual Studio x64 Native Tools Command Prompt icinde:

```bat
cd /d "C:\path\to\HelloMap"
cmake --preset windows-msvc --fresh
cmake --build --preset windows-msvc-release
```

Qt CMake tarafindan otomatik bulunamiyorsa Qt kurulum klasorunu `QTDIR`
ortam degiskeniyle belirtin veya configure komutuna kendi Qt yolunuzu verin:

```bat
set QTDIR=C:\Qt\6.x.x\msvc2022_64
cmake --preset windows-msvc --fresh
```

Cikti:

```text
outputs\windows\Release\bin\HelloMap.exe
```

## Linux ile derleme

```bash
cd "/path/to/HelloMap"
cmake --preset linux-x64 --fresh
cmake --build --preset linux-x64-release
```

Cikti:

```text
outputs/linux/Release/bin/HelloMap
```

## Temizleme

Windows CMD:

```bat
rmdir /s /q build
rmdir /s /q outputs
```

Linux:

```bash
rm -rf build outputs
```

SDK arsivlerini de yeniden indirmek icin `.geokernel-sdk` klasorunu silin.
Onceden cikartilmis uyumlu bir SDK, `GEOKERNEL_ROOT` CMake degiskeniyle verilebilir.

CMake 3.22 veya 3.23 kullaniliyorsa `--fresh` yerine once ilgili build klasorunu silin:

```bash
rm -rf build/linux-x64
cmake --preset linux-x64
```
