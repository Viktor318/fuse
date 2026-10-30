# Fuse – magyar Windows-kiadás

[English](README.md) | **Magyar**

Ez a [Fuse](https://fuse-emulator.sourceforge.net/) (Free Unix Spectrum
Emulator, 1.10.1-es verzió) ZX Spectrum emulátor egy változata, amely a Fuse
saját Windows-felületével, magyarul vagy angolul használható. Magyar
felülettel és magyar billentyűzet-támogatással, BASIC programok
szövegként való betöltésével és mentésével, lejátszási listás AY
zenelejátszóval, egész számú nagyítású teljes képernyővel és több
javítással egészíti ki az eredetit, főleg a Windows hangkimenetén.

Maga az emuláció az eredeti Fuse; ez a változat a körülötte lévő Windows
felületet alakítja át. Az eredeti Fuse leírása a [README](README), változásai
a [ChangeLog](ChangeLog) fájlban vannak (angolul). Ennek a változatnak a
módosításai a [CHANGELOG-fork.hu.md](CHANGELOG-fork.hu.md) fájlban
olvashatók.


## Letöltés

A [Releases](https://github.com/Viktor318/fuse/releases) oldalról töltsd le
a `Fuse-1.10.1-hu.1-windows.zip` fájlt, csomagold ki egy tetszőleges
mappába, és indítsd el a `fuse.exe`-t. Telepíteni nem kell semmit.

A csomagban két program van, közös beállításokkal:

- a `fuse.exe` a mai Windows hangrendszerét, a WASAPI-t használja: kisebb
  a késleltetés, és a hangot egy külön szál adja a hangkártyának;
- a `fuse-directsound.exe` a régebbi DirectSoundot használja, tartaléknak,
  ha egy gépen a WASAPI-val nem szól jól a hang.

## Mit ad hozzá ez a kiadás

### Magyar vagy angol felület

- **Beállítások > Általános > Nyelv**: *Automatikus* (a Windows nyelvét
  követi), *English* vagy *Magyar*. A menükre azonnal érvényes, a már nyitott
  ablakok a következő megnyitáskor jelennek meg az új nyelven.
- Minden menü, párbeszédablak, beállítás és üzenet le van fordítva. Az
  ablakok ott, ahol a magyar szöveg hosszabb, szélesebbek lesznek.

### Billentyűzet

- **Gépelés a Windows billentyűkiosztása szerint** (Beállítások >
  Általános, alapból bekapcsolva): az jelenik meg a Spectrumon, amit
  begépelsz, bármilyen kiosztású a billentyűzeted. Magyar billentyűzeten a
  Shift+2 `"`-t, az AltGr+V `@`-ot ír, és így tovább. Az ékezetes betűk
  ékezet nélkül kerülnek be. A kiterjesztett módot igénylő jeleket
  (`[ ] { } \ | ~ ©`) a program a megfelelő billentyűsorozattal magától
  begépeli.
- **A bal Alt legyen Caps Shift a számbillentyűknél** (alapból bekapcsolva):
  a bal Alt + 0–9 Caps Shift + számot ad (EDIT, kurzorbillentyűk,
  DELETE...). Egyébként az Alt megtartja a Windowsban megszokott szerepét
  (menük, Alt+F4). A numerikus billentyűzet mindig számokat és `+ - * / .`
  jeleket ad.
- **Súgó > Billentyűzet**: nagy felbontású kép a Spectrum billentyűzetéről.
  A Fuse fölött marad, de nem veszi el a fókuszt, így közben tovább lehet
  gépelni; átméretezhető, és megjegyzi a helyét és a méretét.

### BASIC programok szövegként

- **Fájl > BASIC szöveg betöltése...**: szövegfájlból olvas be BASIC
  programot (zmakebas-féle jelölések, BASin `.bas` fájlok, UTF-8 vagy
  Windows-1250 kódolás), és a memóriában lévő program helyére teszi. A
  sorszám nélküli sorokat 10, 20, 30... sorszámmal látja el, az ékezetes
  betűket kicseréli, és ezt jelzi is. Ha épp fut egy program, rákérdez, és
  előbb újraindítja a gépet.
- **Fájl > BASIC mentése szövegként...**: a memóriában lévő programot
  szövegfájlba írja.

### AY zenelejátszó


`.ay` (ZXAYEMUL) zenefájlokat játszik le – sok Spectrum-játék és demó zenéje
megvan ebben a formában –, valamint Pro Tracker 3 (`.pt3`) modulokat. A dal
egy saját Z80-on fut (Andre Weissflog [chips](https://github.com/floooh/chips)
Z80-a) saját AY hanggenerátorral, az emulált géptől függetlenül.

- **Megnyitás**: a *Média > AY zenelejátszó...* menüpont úgy nyitja meg a
  lejátszót, hogy nem szól semmi; a *Fájl > Megnyitás* (vagy a fájl Fuse-ra
  húzása) a fájl dalait a lejátszási lista végére teszi, és az elsőt
  elindítja.
- **Az emulált gépet nem érinti**: a futó program megy tovább, és a hangja a
  zenével együtt hallatszik.
- **Lejátszási lista ablak**: akárhány fájl dalai; Hozzáadás (egyszerre több
  fájl is), Mappa hozzáadása, Eltávolítás (a Delete billentyűvel is), Lista
  törlése, Fel/Le, Lista betöltése és mentése (`.m3u`), fájlok, mappák vagy
  listák behúzása az Intézőből. Dupla kattintás vagy Enter: a dal indítása.
  Az ablak átméretezhető, megjegyzi a helyét, méretét és az oszlopok
  szélességét; a lista megmarad a következő indításra.
- **Lejátszás**: *1 dal* vagy *Teljes lista*, valamint *Ismétlés* (a dalé
  vagy a teljes listáé). Előző / Szünet / Leállítás / Következő (a Szünet
  csak a zenét állítja meg), és csúszka a dalban való tekeréshez (azonnal,
  mindkét irányban).
- **A dalok hosszát a program megméri**: az `.ay` fájlokban megadott
  hosszak gyakran hibásak vagy hiányoznak. Egy dal első lejátszásakor (vagy a
  lista ablak *Hosszak mérése* gombjával) a dal a háttérben, hang nélkül fut,
  amíg a zene ismétlődni nem kezd, és a program megjegyzi a pontos hosszát
  (ez jóval egy másodpercen belül megvan); a zene közben szól tovább. A sosem
  ismétlődő dalok 5 percet kapnak. A körbeérő dal 3 másodperc alatt elhalkul, amikor a
  lejátszó továbblép.
- **Szintek és hang**: a három AY-csatorna szintjelzője (a sztereó mód
  sorrendjében), csatornánként némító jelölőnégyzet, hangerő és sztereó mód
  (ABC, ACB, BAC, BCA, CBA, CAB, Mono és Egyéni) – mind lejátszás közben,
  azonnal állítható.
- **AY keverő** (a lejátszó *Keverő...* gombja, vagy *Beállítások > AY
  keverő...*): a sztereó mód, Egyéni módban a csatornák helye, és
  csatornánként a hangerő és a panoráma, a sztereó mód sorrendjében. A
  játékokra is hat. Az *Alaphelyzet* a hangerőket 100%-ra, a panorámákat a
  sztereó mód szerinti helyre állítja; módváltáskor a panorámák maguktól is
  oda kerülnek.
- **A dalok keverőbeállításai**: a keverő *Mentés...* gombja a dal mellé
  egy `.mix` szövegfájlba menti őket (`Dal.mix`; egy fájl 3. dalához
  `Dal.3.mix`, ez előbbre való a `Dal.mix`-nél; ha a dal mappája nem
  írható, a `%APPDATA%\Fuse\mix` mappába). Ha a lejátszóban be van pipálva a
  *Keverőbeállítások automatikus betöltése a dalokhoz*, a `.mix` fájllal
  rendelkező dal azzal szól; a következő, `.mix` nélküli dalnál visszajönnek
  a saját beállításaid. A `.mix` fájl soha nem változtatja meg a mentett
  beállításokat.
- A zene akkor is szól tovább, ha egy menü vagy párbeszédablak nyitva van, ha
  egy ablakot mozgatsz, vagy ha a Fuse kis méretre van állítva. WASAPI-val
  (`fuse.exe`) a zenének saját hangfolyama van, és egyáltalán nem kell hozzá
  az emuláció; DirectSounddal a zene az emulátor hangjába keveredik, ezért az
  emuláció menükben és ablakmozgatás közben fut tovább (párbeszédablakokban
  nem).

### Ablakok és hang

- **Teljes képernyő** (F11, *Beállítások > Teljes képernyő*): keret
  nélküli, egész számú nagyítással – a nem hivatalos
  [jfroco/fuse](https://github.com/jfroco/fuse) változatból átvéve.
- **Folytatás ott, ahol abbahagytam** (Beállítások > Általános): kilépéskor a
  Fuse elmenti a gépet a betett kazettával és annak állásával együtt, és a
  következő indításkor visszaállítja.
- A főablak, a billentyűzetkép és az AY lejátszó ablakai ott nyílnak meg,
  ahol legutóbb voltak.
- **Rákérdezés kilépéskor** (Beállítások > Általános, alapból bekapcsolva).
- A perifériák beállításainál szürkék azok, amelyek az emulált gépen nem
  lehetnek.
- A menüsor jól látható kijelöléssel jelenik meg.
- Hangkimenet: WASAPI (`fuse.exe`) vagy DirectSound (`fuse-directsound.exe`).
  DirectSounddal nincs régi hang ismételt lejátszása akadás után, nincs
  kattogás.

### Hová menti a fájljait a Fuse

Mindent a **Roaming mappán belüli Fuse mappában** tart: írd be az Intéző
címsorába (vagy a Win+R ablakba), hogy `%APPDATA%\Fuse`. Benne:

| Fájl | Tartalom |
|---|---|
| `fuse.cfg` | a beállítások (a *Beállítások > Mentés* paranccsal, vagy kilépéskor, ha a *Beállítások > Általános > Beállítások automatikus mentése* be van kapcsolva) |
| `fuse-windows.ini` | az ablakok helye és mérete |
| `fuse-ay-playlist.m3u` | az AY lejátszási lista |
| `fuse_ay_lengths.txt` | az AY dalok mért hossza |
| `fuse-last-session.*` | az utolsó munkamenet (a *Folytatás ott, ahol abbahagytam* beállítással) |

A korábbi verziók által a felhasználói mappába mentett fájlokat a program az
első indításkor ide költözteti.

## Fordítás a forráskódból

A Windows-programot Linuxon vagy WSL-ben (Windows Subsystem for Linux)
lehet lefordítani, MinGW-w64-gyel:

```
sudo apt install gcc-mingw-w64-x86-64 binutils-mingw-w64-x86-64 \
     libz-mingw-w64-dev autoconf automake libtool pkg-config flex bison \
     perl zip git
./build-windows-cross.sh
```

A szkript letölti és lefordítja a libspectrum könyvtárat, majd a Fuse-t a
Windows-felülettel kétszer (WASAPI: `fuse.exe`, DirectSound:
`fuse-directsound.exe`), és ezeket a szükséges DLL-ekkel, ROM-okkal és
adatfájlokkal együtt a `~/fuse-windows/Fuse-windows-<ág>.zip` fájlba
csomagolja. `FUSE_AUDIO_DRIVERS=wasapi` esetén csak az egyiket fordítja le. A forrás egy másolatán dolgozik. A WSL-hez kapcsolódó VS Code-ból
való fordítás beállításai a `.vscode` mappában vannak (Ctrl+Shift+B).

## Licenc

A Fuse szabad szoftver, a GNU General Public License 2-es vagy újabb
verziója alatt (lásd [COPYING](COPYING)). Az AY lejátszó a ZEsarUX emulátor
módján indítja a dalokat (`ayplayer.c`, César Hernández Bañó ZEsarUX-a
alapján, GPL 3-as vagy újabb verzió), ezért ez a kiadás egészében a GNU GPL
3-as vagy újabb verziója alatt terjeszthető.

## Köszönet

- Philip Kendall-nak, Fredrick Meuniernek és mindenkinek, aki a
  [Fuse](https://fuse-emulator.sourceforge.net/) elkészítésében részt vett.
- César Hernández Bañónak a [ZEsarUX](https://github.com/chernandezba/zesarux)
  emulátorért.
- jfroco-nak a [jfroco/fuse](https://github.com/jfroco/fuse) egész számú
  nagyítású teljes képernyőjéért.
