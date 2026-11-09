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
a `Fuse-1.10.1-hu.3-windows.zip` fájlt, csomagold ki egy tetszőleges
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
  Általános; amíg nem állítod be, magyar felülettel bekapcsolva, angollal
  kikapcsolva): az jelenik meg a Spectrumon, amit
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
- **Billentyűzet beolvasása, amikor a program olvassa** (alapból
  bekapcsolva): a képkocka futása közben lenyomott billentyűk a Spectrum
  program következő billentyűzet-olvasásakor odaérnek, nem csak a következő
  képkocka elején (ahogy az upstream Fuse SDL 2-es változatában).
- **Spectrum 128 keypad** (Beállítások > Perifériák >
  Általános, alapból kikapcsolva): a Spectrum 128 csatlakoztatott keypadje.
  A 128 BASIC szerkesztőjének egyes szerkesztési funkciói csak a keypaden
  voltak; ezeket a PC szerkesztőbillentyűi adják a 128-on és a +2-n (a
  +2A és a +3 ROM-ja nem használja a keypadet):

  | Billentyű | 128 BASIC szerkesztő |
  |---|---|
  | Home / End | a sor eleje / vége |
  | Ctrl + Home / End | a program első / utolsó sora |
  | Ctrl + ← / → | előző / következő szó |
  | Page Up / Page Down | 10 sorral fel / le |
  | Delete | a kurzor alatti karakter törlése |
  | Ctrl + Backspace / Delete | törlés a szó elejéig / végéig |
  | Shift + Backspace / Delete | törlés a sor elejéig / végéig |
  | Insert | az Options menü (CMND) |
  | Ctrl + Insert | váltás a felső és az alsó képernyőrész között (TOGGLE) |

  Az Insert és a Delete ilyenkor csak RZX felvétel közben gyorsbillentyűje
  az RZX menüpontoknak.
- **Súgó > Billentyűzet**: nagy felbontású kép a Spectrum billentyűzetéről.
  A Fuse fölött marad, de nem veszi el a fókuszt, így közben tovább lehet
  gépelni. Öt mérete van, 100%-tól 200%-ig (a 200% a kép saját felbontása,
  így mindig éles): a képre duplán kattintva a következőre vált, a keret
  húzásakor a legközelebbire ugrik; a képernyőre ki nem férő méreteket
  kihagyja. Megjegyzi a helyét és a méretét. Bekapcsolt Spectrum 128
  keypad mellett (128, +2) a keypad a billentyűzet jobb oldalán látszik, a
  billentyűk alatt szürkével a funkcióikat adó PC billentyűkkel.
- **Automatikus tűz** (*Beállítások > Joystickok > Joystick 1 / Joystick 2
  / Billentyűzet...*): amíg a tűzgomb le van nyomva, a Spectrum felváltva
  lenyomottnak és felengedettnek látja, 3-3 képkockáig (másodpercenként
  kb. 8 lövés), bármilyen joysticktípussal (Kempston, Sinclair, Cursor,
  Fuller, Timex).

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
megvan ebben a formában –, tracker modulokat (Pro Tracker 1–3, Vortex
Tracker II és szöveges moduljai, Sound Tracker és Sound Tracker Pro, ASC
Sound Master, SQ-Tracker, Pro Sound Creator és Maker, Sound Tracker 1 és
3, Global Tracker, Fast Tracker), regiszterfelvételeket (PSG, YM, VTX) és
két vagy három AY chipes **TurboSound** zenéket (`.ts`, 6 vagy 9 csatorna).
Az `.ay` dal
egy saját Z80-on fut (Andre Weissflog [chips](https://github.com/floooh/chips)
Z80-a) saját AY hanggenerátorral, az emulált géptől függetlenül.

- **Megnyitás**: a *Média > AY zenelejátszó...* menüpont úgy nyitja meg a
  lejátszót, hogy nem szól semmi; a *Fájl > Megnyitás* (vagy a fájl Fuse-ra
  húzása) a fájl dalait a lejátszási lista végére teszi, és az elsőt
  elindítja.
- **TurboSound lejátszó** (*Média > TurboSound lejátszó...*): a két vagy
  három AY chipes zenék (`.ts` fájlok, TurboSoundot tartalmazó Pro Tracker 3
  modulok, két modult tartalmazó Vortex Tracker szövegfájlok) saját
  lejátszóablakban nyílnak meg, saját lejátszási listával (és `.m3u`-val):
  fájl, szerző, formátum (pl. „TurboSound – PT3 + PT3 (2 AY, 6 csatorna)”),
  pozíció, ugyanazok a gombok és lejátszási módok, szintjelzők nélkül.
  Egyszerre egy lejátszó szól; az `.ay` fájlok mindig az AY lejátszóban
  maradnak. Mindkét lista mutatja a dalok csatornaszámát (*Csat.*: 6 vagy
  9; az AY listában 3, vagy 6, ha egy `.ay` dalról a méréskor kiderül, hogy
  a második AY-t is használja).
- **TurboSound keverő** (a TurboSound lejátszó *Keverő...* gombja, vagy
  *Beállítások > TurboSound keverő...*): fő hangerő, közös sztereó mód, és
  minden chip minden csatornájához panoráma, ki-be kapcsoló, szintjelző és
  hangerő; két vagy három chipcsoport, ahogy a dal. A beállításai sajátok
  (az AY keverő nem hat a TurboSound dalokra), a `.mix` fájljai mind a
  kilenc csatornát tárolják; az AY keverő `.mix` fájljának A, B és C
  értékei minden chipre érvényesek.
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
- **Turbó** (F12, *Gép > Turbó*): az emuláció olyan gyorsan fut, ahogy
  csak tud, hang nélkül (mint a kazetta gyorsbetöltésekor); az F12 újra
  megnyomva visszavált a normál sebességre. Az állapotsor mutatja a
  sebességet. Nem kerül mentésre.
- **Legutóbbi fájlok** (*Fájl > Legutóbbi fájlok*): az utolsó 10 megnyitott
  fájl (*Fájl > Megnyitás*, a Fuse-ra húzás, a parancssor vagy a *Média*
  menük útján), a legújabb elöl; egy kattintással vagy az 1–9, 0
  billentyűvel újra megnyitható. A már nem létező fájl lekerül a listáról. A
  lista a `fuse.cfg`-ben van.
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
| `fuse-ts-playlist.m3u` | a TurboSound lejátszási lista |
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

A szkript letölti és statikus könyvtárként lefordítja a libspectrumot, a
libpng-t és a bzip2-t, majd a Fuse-t a
Windows-felülettel kétszer (WASAPI: `fuse.exe`, DirectSound:
`fuse-directsound.exe`), és ezeket a két szükséges DLL-lel (`zlib1.dll`,
`libwinpthread-1.dll`), a ROM-okkal és
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
