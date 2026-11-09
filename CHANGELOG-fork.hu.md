# A magyar Windows-kiadás változásai

[English](CHANGELOG-fork.md) | **Magyar**

Ennek a változatnak a módosításai a Fuse 1.10.1-hez képest (a
v1.10.0-hu.1-ig: 1.10.0). Magának a Fuse-nak a változásai a
[ChangeLog](ChangeLog) fájlban vannak (angolul).

## Kiadatlan

### Spectrum 128 keypad

- A keypad többi szerkesztőfunkciója: a Ctrl + Home / End a program első /
  utolsó sorára ugrik, a Ctrl + Backspace / Delete a szó elejéig / végéig
  töröl, a Ctrl + Insert vált a felső és az alsó képernyőrész között
  (TOGGLE). A sor elejéig / végéig törlés a Shift + Backspace / Delete-re
  került (eddig Ctrl + Backspace / Delete volt).

### Felhasználói felület

- A *Súgó > Névjegy* és a `fuse --version` ennek a kiadásnak a verzióját
  mutatja („Fuse 1.10.1-hu.3”), nem csak az alapul szolgáló Fuse-ét; a
  `fuse.exe` verzióinformációja is.
- **Legutóbbi fájlok** (*Fájl > Legutóbbi fájlok*): az utolsó 10 megnyitott
  fájl, a legújabb elöl, 1–9, 10 számozással, a hosszú útvonalak közepe
  rövidítve; alattuk a *Lista törlése*. A *Fájl > Megnyitás*, a Fuse-ra
  húzás, a parancssor, valamint a *Média > Kazetta > Megnyitás*, a *Média >
  Lemez > Behelyezés* és a kártyák menüi útján megnyitott fájlok kerülnek
  rá; újra a *Fájl > Megnyitás* módján nyílnak meg. A már nem létező fájl
  üzenettel lekerül a listáról. A lista a `fuse.cfg`-ben van
  (`recentfiles`, az útvonalak `|` jellel elválasztva), és minden
  változáskor kiíródik, a többi beállítás nélkül.
- **Turbó** (F12, *Gép > Turbó*, bekapcsolva pipa jelzi): az emuláció
  olyan gyorsan fut, ahogy csak tud, mint a kazetta gyorsbetöltésekor (az
  is az a megoldás), hang nélkül; az állapotsor mutatja a sebességet
  (több ezer százalék). Az F12 újra megnyomva visszavált a beállított
  sebességre, és visszajön a hang. Nem beállítás: a Fuse mindig normál
  sebességgel indul. Ha közben gépet váltasz, a hang akkor sem kapcsol be.
- **Kézikönyv és leírás a csomagban**: a `doc` mappában weboldalként ott
  van a Fuse kézikönyve (a `man/fuse.1`-ből), valamint ennek a kiadásnak a
  README-i és változásnaplói. A *Súgó > Kézikönyv* a kézikönyvet, a
  *Súgó > A magyar kiadás újdonságai* a README-t (magyar felülettel a
  magyart) nyitja meg a böngészőben; ha a fájl hiányzik, üzenet jelzi. A
  `build-windows-cross.sh` pandockal készíti el őket, pandoc nélkül
  figyelmeztetéssel kihagyja.

### Billentyűzet

- A *Gépelés a Windows billentyűkiosztása szerint* alapból be van
  kapcsolva, ha a felület magyar (*Nyelv*: Magyar, vagy Automatikus magyar
  Windowson), egyébként ki, ahogy a README-k írták; eddig mindig ki volt
  kapcsolva. Ha a beállításokban, a `fuse.cfg`-ben (`windowslayout`) vagy
  a parancssorban (`--windows-layout`, `--no-windows-layout`) beállítod,
  úgy marad; addig a nyelvet követi, és nem kerül mentésre.
- **Billentyűzetkép** (*Súgó > Billentyűzet*): öt méret, 100%, 125%, 150%,
  175% és 200%, ahol a 200% a kép saját felbontása (1336 × 584), a 100% a
  fele, így a kép csak kicsinyítve jelenik meg, és éles marad. A képre
  duplán kattintva a következő méretre vált (a 200% után a 100%-ra); a
  keret húzásakor a legközelebbi méretre ugrik, a húzott oldal mozog, a
  szemközti a helyén marad. A monitor munkaterületére ki nem férő
  méreteket kihagyja. A méret a `fuse-windows.ini`-ben (`step`) marad meg
  a hellyel együtt; egy korábbi változat által mentett méretből a
  legközelebbi lépés lesz. Először 200%-on nyílik meg, vagy a legnagyobb
  kiférő méreten.
- **Spectrum 128 keypad a billentyűzetképen**: a 128-on és a +2-n,
  bekapcsolt *Spectrum 128 keypad* mellett a keypad a billentyűzet jobb
  oldalán látszik, ugyanakkora magasságban és ugyanazokban a méretekben, a
  billentyűk alatt szürkével a funkcióikat adó PC billentyűkkel. A gép vagy
  a beállítás változásakor megjelenik, illetve eltűnik; az ablak bal
  oldala a helyén marad. A kép ehhez a kiadáshoz készült (`lib/keypad.png`,
  a `data/keypad.svg`-ből).

### Joystickok

- **Automatikus tűz**: *Automatikus tűz* jelölőnégyzet a Joystick 1, a
  Joystick 2 és a billentyűzet-joystick beállításaiban (*Beállítások >
  Joystickok*). Amíg a tűzgomb le van nyomva, a Spectrum felváltva
  lenyomottnak és felengedettnek látja, 3-3 képkockáig (másodpercenként
  kb. 8 lövés), minden joysticktípussal (Kempston, Sinclair 1 és 2,
  Cursor, Fuller, Timex 1 és 2). A `fuse.cfg`-ben tárolódik
  (`joystick1autofire`, `joystick2autofire`, `joystickkeyboardautofire`).

### Upstream Fuse

- Javítás az upstream `master` ágból: a lemezmeghajtó motorja akkor is
  forog, ha nincs benne lemez; a lemez betétele vagy kivétele csak az
  indexjeleket indítja vagy állítja le. A +D, a DISCiPLE és az Opus egy
  motorjele mindkét meghajtót, a Beta 128-é mind a négyet hajtja, mint a
  valódi gépen; a WD1770/1772 alaphelyzetbe állítása leállítja a motort; a
  DISCiPLE beállítja a lemezvezérlő sűrűségjelét (DDEN).

## v1.10.1-hu.3 – 2026-11-09

### TurboSound

- **TurboSound zenék** két vagy három AY chipen: a `.ts` fájlok (bármilyen
  tracker két vagy három modulja `02TS` / `03TS` lezárással, ahogy a
  Vortex Tracker, a ZXTune és – három chippel, 9 csatornán – a ZX
  Spectrum Next zenéi), a TurboSoundot tartalmazó Pro Tracker 3 modulok (a
  második AY a tükrözött mintákat játssza, mint a ZXTune-ban), a két
  modult tartalmazó Vortex Tracker szövegfájlok, és a második AY-t
  kiválasztó (FFFDh portra FEh-t író) `.ay` fájlok. Két chipnél mindegyik
  a szint 2/3-án, háromnál a felén szól, így az összeg nem torzul. A
  modulok szövegei egyszer látszanak, ha egyeznek, különben „/”-lel
  összefűzve; a hossz és az ismétlés az első modulé, mint a ZXTune-ban.
- **TurboSound lejátszó** (*Média > TurboSound lejátszó...*, az AY
  zenelejátszó alatt), saját lejátszási lista ablakkal, mentett listával
  (`fuse-ts-playlist.m3u`) és ablakhelyekkel. Mutatja a fájlt, a szerzőt,
  a formátumot („TurboSound – PT3 + PT3 (2 AY, 6 csatorna)”; a
  TurboSoundot tartalmazó Pro Tracker 3 modulnál „TurboSound – PT3 (2 AY,
  6 csatorna)”) és az egyéb adatokat, a dalt és a pozícióját; Előző /
  Szünet / Leállítás / Következő, *Lista* és *Keverő...*, és saját
  lejátszási mód (1 dal / teljes lista, ismétlés). A lista és a keverő
  vele együtt kicsinyül és záródik, és a *Folytatás ott, ahol
  abbahagytam* beállítással induláskor újra megnyílnak (az utolsó dalon
  állva), mint az AY lejátszónál.
- **Megnyitás**: a `.ts` fájlok, a TurboSoundot tartalmazó Pro Tracker 3
  modulok és a két modult tartalmazó Vortex Tracker szövegfájlok a
  TurboSound lejátszóban nyílnak meg (*Fájl > Megnyitás*, Fuse-ra húzás,
  parancssor); az `.ay` fájlok mindig az AY lejátszóban, a második AY-t
  használók is. Mindkét lista csak a saját fajtáját fogadja el: a másik
  listába egyenként hozzáadott fájlt üzenettel visszautasítja, egy mappa
  ilyen fájljait csendben kihagyja. Egyszerre egy lejátszó szól: az
  egyikben indított dal leállítja a másikat.
- **Csatornák oszlop** (*Csat.*) mindkét listában: a TurboSound listában 6
  vagy 9; az AY listában 3, vagy 6 annál az `.ay` dalnál, amelyről a hossz
  mérésekor kiderül, hogy a második AY-t is használja (a hosszal együtt
  megmarad; a korábban mért dalok 3 csatornásnak számítanak).
- **TurboSound keverő** (a TurboSound lejátszó *Keverő...* gombja, vagy
  *Beállítások > TurboSound keverő...*): fő hangerő, egy közös sztereó mód
  az összes chipre, és minden AY chiphez egy csoport, csatornánként egy
  oszloppal – a panoráma, a csatorna betűje, kikapcsoló jelölőnégyzet, a
  szintjelző (zöld, sárga, piros) a hangerő mellett, alatta a hangerő
  százalékban. Annyi chipet mutat, ahányat a dal használ, és ehhez jobbra
  keskenyebb vagy szélesebb lesz; a címsorban a dal, alul a használt
  beállítások látszanak („Beállítások: Dal.mix”, vagy „Alapértelmezett”).
  Az *Egyéni*-n kívüli sztereó módok minden chip A, B és C csatornáját oda
  teszik, ahová az AY keverő; *Egyéni* módban a kilenc panoráma egyenként
  állítható.
- A TurboSound dalok csak a TurboSound keverő beállításaival szólnak (a
  `fuse.cfg`-ben az AY keverőétől külön): az AY keverő, az AY lejátszó
  hangereje és csatornakapcsolói nem hatnak rájuk, a TurboSound keverő
  pedig nem hat az AY lejátszó dalaira és a játékokra.
- **A TurboSound keverő `.mix` fájljai** a fő hangerőt és a három chip
  minden csatornáját tárolják (szól-e, hangerő, panoráma); az első chip
  kulcsai az AY keverőéi, így az AY keverő ezeket (és a sztereó módot)
  beolvassa, a többit kihagyja. Az AY keverő `.mix` fájljának A, B és C
  értékei minden chipre érvényesek. A *Keverőbeállítások automatikus
  betöltése a dalokhoz* beállítással a TurboSound dal a saját `.mix`
  fájljával szól, amelyet a program az AY lejátszóéval azonos módon keres
  meg.

### AY lejátszó

- Mono módban magyarul a C csatorna szintjelzőjének felirata „K” volt;
  mostantól újra „C”.

### Pillanatképek

- Ha a pillanatkép olyan formátumba kerül, amely nem tud mindent tárolni
  (`.sna`, `.z80`), a mentés után a Fuse megmondja, mi maradt ki: „A
  SpecDrum és a Beta 128 állapota nem menthető .z80 fájlba.”, vagy „A gép
  típusa (Spectrum 128K) nem menthető .sna fájlba: betöltéskor Pentagon
  128K gépként indul.”, majd „Ha mindent meg akarsz őrizni, ments SZX
  formátumban.” Korábban azt írta, hogy a pillanatkép valószínűleg nem fog
  működni, mintha a fájl hibás lenne. A gyakorlatban jelentéktelen apró
  eltérésekről (a T-állapotok száma, HALT) már nem szól.

## v1.10.1-hu.2 – 2026-11-07

### ROM-ok

- A csomag tartalmazza a Pentagon (`128p-0.rom`, `128p-1.rom`,
  `gluck.rom`), a Scorpion ZS 256 (`256s-0.rom`...`256s-3.rom`), a TR-DOS
  (`trdos.rom`) és az Interface 1 (`if1-1.rom`, `if1-2.rom`) ROM-jait, így
  a 128K-s `.sna` pillanatképek (amelyek Pentagon 128K-ként töltődnek be),
  a Pentagon és a Scorpion gép, a Beta 128 lemezinterfész és az Interface 1
  külön beszerzés nélkül működik. Az upstream Fuse ezeket nem tartalmazza;
  lásd `roms/README.copyright`.

### Fájl-ablakok

- A mentés-ablakok csak azokat a típusokat sorolják fel, egyenként,
  amelyeket írni tudnak, és a kiterjesztés nélkül (vagy másféle fájl
  kiterjesztésével) beírt név megkapja a választott típus vagy az
  alapértelmezett kiterjesztését: pillanatkép `.szx` (még `.z80`, `.sna`),
  kazetta `.tzx` (még `.tap`, `.pzx`, `.csw`), lemez a meghajtó típusa
  szerint (+3 `.dsk`, Beta `.trd`, +D és DISCiPLE `.mgt`, Opus `.opd`,
  Didaktik `.d80`, mindegyik még `.udi`; az újra mentett lemezt a saját
  típusában ajánlja), Microdrive `.mdr`, képernyőkép `.scr`, `.mlt`, `.png`,
  `.svg`, RZX `.rzx`, film `.fmf`, AY napló `.psg`, profiladat `.txt`, BASIC
  szöveg `.bas` (vagy `.txt`). A *Minden fájl* a lista végén megmarad; a
  bináris adat a beírt néven mentődik.
- A megnyitás-ablakok a művelethez illő típust választják ki előre:
  kazetta, pillanatkép, RZX felvétel, lemez, Microdrive, Dock (`.dck`),
  Interface 2 és ROM (`.rom`, új típus a listában), merevlemez (`.hdf`),
  képernyőkép. A *Fájl > Megnyitás* továbbra is minden támogatott fájlt
  mutat.
- Minden ablak megjegyzi a legutóbb választott típust (amíg a Fuse fut).

### Billentyűzet

- **Billentyűzet beolvasása, amikor a program olvassa** (*Beállítások >
  Általános*, alapból bekapcsolva): ahogy az upstream Fuse SDL 2-es
  változata, most a Windowsos is feldolgozza a főablak várakozó
  billentyűleütéseit és -felengedéseit, amikor a Spectrum program a
  billentyűzetet olvassa (egy képkockában az első olvasáskor, utána
  legfeljebb 5 ms-onként), nem csak képkockánként egyszer, a képkocka
  elején. A képkocka futása közben lenyomott billentyű így hamarabb
  odaér. A menük, a gyorsbillentyűk (F2...F11, Insert, Delete, Pause), az
  Alt-os kombinációk, az egérüzenetek és a más ablaknak szóló billentyűk
  továbbra is a következő képkocka elején, sorrendben kerülnek sorra.
  Kikapcsolva a billentyűzetet a program képkockánként egyszer olvassa,
  mint eddig.
- **Spectrum 128 keypad** (*Beállítások > Perifériák >
  Általános*, alapból kikapcsolva), a 128-on és a +2-n: a PC
  szerkesztőbillentyűi adják a 128 BASIC szerkesztőjének azokat a
  funkcióit, amelyek csak a keypaden voltak. Home / End: a sor eleje /
  vége; Ctrl + bal / jobb nyíl: előző / következő szó; Page Up / Page Down:
  10 sorral fel / le; Delete: a kurzor alatti karakter törlése; Ctrl +
  Delete / Ctrl + Backspace: törlés a sor végéig / elejéig; Insert: az
  Options menü (CMND). A keypad protokollja az EightyOne emulációját
  követi, hosszabb várakozással a bitek között (a ROM-ot ott megszakíthatja
  a képkocka-megszakítás). Amíg csatlakoztatva van, az Insert és a Delete
  csak RZX felvétel közben gyorsbillentyűje az RZX menüpontoknak. A többi
  gépen a beállítás szürke. A beállításokkal együtt mentődik, és
  pillanatkép betöltése (amely nem tudja tárolni) nem kapcsolja ki.

### AY keverő

- Új **AY keverő** ablak (az AY lejátszó *Keverő...* gombja, *Beállítások >
  AY keverő...*): a sztereó mód, Egyéni módban a csatornák helye (a többi
  módban szürkén látszik), és csatornánként a hangerő és a **panoráma**, a
  sztereó mód sorrendjében. A panoráma egyensúlyként működik: középen
  mindkét oldal teljes erővel szól, oldalra húzva a másik oldal halkul; a
  mód szerinti helyen a csatorna úgy szól, mint eddig. A játékokra és az AY
  lejátszóra is hat. *Alaphelyzet*: a hangerők 100%, a panorámák a mód
  szerinti helyre (módváltáskor is így lesz). A több ablakban is állítható
  beállítások azonnal követik egymást.
- **`.mix` fájlok**: a keverő *Mentés...* gombja a keverőbeállításokat a dal
  mellé egy szövegfájlba menti (`Dal.mix`, vagy egy fájl 3. dalához
  `Dal.3.mix`; ha a mappa nem írható, a `%APPDATA%\Fuse\mix` mappába). Ha az
  AY lejátszóban be van pipálva a *Keverőbeállítások automatikus betöltése a
  dalokhoz*, a dal a saját `.mix` fájljával szól, egy `.mix` nélküli dalig; a
  Fuse mentett beállításait ezek nem változtatják meg. A keverő címsora
  mutatja a használt fájlt.
- Az AY lejátszó *Keverő...* gombja megnyitja a keverőt, vagy bezárja, ha
  nyitva van. A `.mix` fájlok automatikus betöltése alapból be van kapcsolva.
- Az AY sztereó mód ismét a *Beállítások > Hang* lapon is ott van (*AY
  sztereó mód*, ahogy a Fuse-ban, most mind a nyolc móddal); azonnal követi
  a keverőt és az AY lejátszót, a *Mégse* visszaállítja.
- Az AY lejátszó ablaka ugyanolyan keskeny, mint a csatornahangerők előtt
  (azok most a keverőben vannak), hosszabb szintjelzőkkel; a *Keverő...*
  gomb a sztereó mód mellett van.
- A *Beállítások > Hang* lapon az AY-beállítások közül az AY hangerő és a
  sztereó mód maradt; a helyek és a csatornahangerők a keverőben vannak.

### AY zeneformátumok

- **PSG fájlok** (`.psg`, emulátorok AY-regiszter felvételei) lejátszása az
  AY lejátszóban: a hosszuk pontosan ismert, a dal ott véget ér.
  Ugyanúgy megnyithatók, mint a többi zenefájl, felvehetők a lejátszási
  listára (mappával is), és szerepelnek a fájlszűrőkben (*AY zenefájlok
  (\*.ay;\*.pt3;\*.psg)*). A PSG fájlban nincs cím és szerző: a fájl neve
  látszik.
- **YM fájlok** (`.ym`: YM2!, YM3!, YM3b, YM5!, YM6!, az Atari ST
  ST-Sound formátuma, többnyire LHA archívumba csomagolva) és **VTX
  fájlok** (`.vtx`, Vortex) lejátszása is az AY lejátszóban, címmel,
  szerzővel és megjegyzéssel, pontos hosszal és ismétlési ponttal. A
  képkockák 50 Hz-en szólnak, a periódusokat a Fuse a fájl chipjének
  órajeléről (Atari ST: 2 MHz, Amstrad CPC: 1 MHz...) a Spectrum AY-ára
  számolja át. A YM5/YM6 digitális dobjai és különleges effektjei nem
  szólnak (a zene igen). Ehhez a Fuse saját LHA (-lh5-) kicsomagolót
  kapott (a lhasa és az ar002 módszere szerint).
- **Vortex Tracker II szöveges modulok** (`.vt2`, és a `[Module]` sorral
  kezdődő `.txt` fájlok) lejátszása az AY lejátszóban, ugyanúgy, mint a
  `.pt3` moduloké, címmel és szerzővel. A többi `.txt` fájlt a Fuse nem
  veszi zenének. A TurboSound szöveges modulnak (két modul) az első
  modulja szól, ahogy a `.pt3`-nál.
- További tizenkét tracker moduljai szólnak az AY lejátszóban: **Sound
  Tracker** (`.stc`), **Sound Tracker Pro** (`.stp`), **Pro Tracker 1**
  (`.pt1`) és **2** (`.pt2`), **ASC Sound Master** (`.asc`, és első
  változata, `.as0`), **SQ-Tracker** (`.sqt`), **Pro Sound Creator**
  (`.psc`), **Pro Sound Maker** (`.psm`), **Sound Tracker 1** és **3**
  (`.st1`, `.st3`), **Global Tracker** (`.gtr`) és **Fast Tracker**
  (`.ftc`), címmel (és szerzővel, ahol a formátumban van). A lejátszók a ZXTune lejátszóinak átiratai; a ZXTune
  mintamoduljain minden képkocka minden AY-regisztere egyezik egy
  ZXTune-buildével, és ugyanígy a hosszak és az ismétlési pontok is.
- A trackermodulok (a `.pt3` és a Vortex szöveges modulok is) hosszát a
  Fuse mostantól a modulból veszi, ahogy a ZXTune: az utolsó pozíció
  végéig tart, és az ismétlési pozíciótól megy tovább. Eddig a hangból
  mérte, ami korábban is megtalálhatta az ismétlődést (pl. a „Speccy 2”
  7 másodperccel rövidebbnek látszott). A korábban mért hosszakat a dal
  lejátszásakor, illetve a *Hosszak mérése* gombra újraméri.
- Az AY lejátszó formátumai a Fuse-on belül közös felületet kaptak, így
  az újabbak egyenként hozzáadhatók. A fájlablakok *AY zenefájlok
  (\*.ay;\*.pt3;\*.stc;\*.ym;...)* néven sorolják fel őket.
- A VTX fájlban tárolt sztereó elrendezés (Mono, ABC, ACB, BAC, BCA, CAB,
  CBA) megjelenik a lejátszó *Egyéb* sorában („Sztereó: ACB”). Ha a
  *Keverőbeállítások automatikus betöltése a dalokhoz* be van kapcsolva, a
  dal idejére be is állítja, ahogy egy `.mix` fájl tenné (a hangerők a
  felhasználóéi maradnak; a keverő címsora mutatja a fájlt és a módot); a
  dal saját `.mix` fájlja erősebb. A mentett beállítások nem változnak.

### AY lejátszó ablakai

- Kilépéskor a Fuse összes ablaka (AY lejátszó, lista, keverő,
  billentyűzet) egyszerre tűnik el. A keverő néha még egy pillanatig a
  képernyőn maradt, amíg a Fuse lezárta a hangot és elmentette a
  beállításokat.
- A *Lista* és a *Keverő...* gomb a kicsinyített ablakot visszahozza és
  előtérbe teszi (nem zárja be); a látható ablakot bezárja, a zártat
  megnyitja.
- A Keverő ugyanúgy a Lejátszóhoz tartozik, mint a Lista: vele együtt
  kicsinyítődik, jön vissza és zárul be. A Lejátszó nélkül (*Beállítások >
  AY keverő...*) továbbra is magában is megnyitható, pl. egy játék
  zenéjéhez.
- A Lejátszó bezáráskor megjegyzi, hogy a Lista és a Keverő nyitva volt-e
  vele, és legközelebb vele együtt az nyílik meg, ami nyitva volt (eddig
  a Lista mindig megnyílt). Az első alkalommal a Lista megnyílik, a Keverő
  nem.
- Az AY lejátszó, a Lista, a Keverő és a Billentyűzet (*Súgó >
  Billentyűzet*) helyét a Fuse folyamatosan megjegyzi, és kilépéskor azt
  is, melyik volt nyitva. Ha a *Folytatás ott, ahol abbahagytam* be van
  kapcsolva, induláskor ugyanezek nyílnak meg (egymástól függetlenül,
  mindig normál méretben); a Lejátszó az utolsó dalon áll, lejátszás
  nélkül.
- A digitális (mintavételezett hangot játszó) AY dalok hossza is
  lemérhető: ezek sosem ismétlődnek bájtra pontosan, ezért a Fuse a
  csatornák hangerejének menetét hasonlítja össze (pl. a KDF Soft „AY
  Megademo Digital Part 2” 1:47). A digitális rész a dal közepén is
  kezdődhet (X-agon of Phantasy „Digitune 1”: 1:42). A korábban le nem
  mérhető dalokat a Fuse újra megméri.
- A hosszmérés a hang és zaj nélkül mozdulatlanul álló hangerőt csendnek
  veszi: az így véget érő dalok ott érnek véget, nem ismétlik a csendet.
- A korábbi változatokkal mért hosszak megmaradnak és látszanak, de a dal
  lejátszásakor, illetve a *Hosszak mérése* gombra a Fuse újraméri őket
  (az új méréssel pontosabbak lehetnek).
- Új *Újramérés* gomb a lejátszási listában (a *Hosszak mérése* alatt,
  sosem szürke): a kijelölt dalokat, kijelölés nélkül az aktuálisat méri
  újra, a már ismert hosszukat is. A futó mérést a *Mérés leállítása*
  állítja meg. Az ablak legkisebb magassága ennyivel nőtt.
- Az `OUT (FDh),A` utasítással mintákat író digitális dalok már nem
  némulnak el az első pillanatok után (X-agon of Phantasy „Digitune 1” és
  „Digitune 2”): az F0h vagy nagyobb mintaérték miatt a lejátszó a
  TurboSound-chipválasztásnak vette az írást, és elhagyta az AY chipet.
  Most csak az FEh és az FFh választ TurboSound-chipet, a többi 16 vagy
  nagyobb érték egyik regisztert sem választja ki, ahogy az AY-on is. A
  „Digitune 2” most 2:16 hosszú.
- A digitális dalok hosszmérése a dal utáni csendet már nem veszi rövid
  ismétlődésnek.
- A szintjelzők a digitális dalokat is mutatják (hang és zaj nélkül a
  hangerő változását).
- A lejátszó dal-sorában és a lista sorszámánál a gondolatjel (–) és a
  » jel minden kódlapon helyesen jelenik meg.

### Fájlok és képernyőképek

- *Fájl > Képernyőkép > Képernyő mentése PNG-ként...*: a képernyőkép PNG
  fájlba is menthető (a választott szűrővel).
- A `.bz2` tömörítésű fájlok (pl. `játék.tzx.bz2`) is megnyithatók.
- Mindkettő az exe-be van fordítva, nem kell hozzájuk újabb DLL.
- A szűrő- és a gépválasztó ablak címe is magyarul jelenik meg.

### Unicode felület

- A felület minden szövege (ablakcímek, menük, párbeszédablakok) és a
  fájlnevek UTF-8-ban mennek a Windowsnak (Windows 10 1903-tól), így az
  „ő” és „ű” nem magyar nyelvű Windowson is helyesen látszik, nem „o” és
  „u”-ként, és bármilyen nyelvű fájlnév megnyitható. Magyar Windowson a
  megjelenés nem változik.
- A korábban elmentett ékezetes útvonalakat (a `fuse.cfg`-ben és az AY
  lejátszó mentett listájában) a Fuse automatikusan átalakítja; a régi
  kódlapú `.m3u` listák és a zenefájlok régi kódlapú címei, szerzői is
  helyesen jelennek meg.
- Ahol a Windows nem a program UTF-8 kódlapját használja, ott a Fuse
  Unicode-dal adja át a szöveget: a listák (pl. az AY lejátszási lista)
  oszlopfejlécei és sorai, az állapotsor, a fájl- és mappaválasztó
  ablakok, a behúzott fájlok neve és a saját rajzolású menüsor.
- Magyar felületen is működik a menüpontok engedélyezése és szürkítése
  (eddig a lefordított menüben nem találta meg a pontokat).

### SpecDrum és Covox

- Nem száll el az emulátor, ha bekapcsolt SpecDrum vagy Covox mellett egy
  program hang nélkül ír az eszköz portjára (gyors betöltés közben, vagy ha
  a hang ki van kapcsolva). Ez történt például a SpecDrum *KIT EDITOR*
  betöltőjénél. A hiba az upstream Fuse-ban is megvan.

### Pillanatfelvételek

- Pillanatfelvétel betöltésekor a *Perifériák: hang* lapon bekapcsolt
  eszközök (Fuller Box, Melodik, SpecDrum, Covox, uSpeech) bekapcsolva
  maradnak. A legtöbb formátum nem tárolja őket (a `.z80` például a
  SpecDrumot sem), ezért eddig a betöltés kikapcsolta őket. Ha a
  pillanatfelvétel maga kér egy eszközt, az továbbra is bekapcsol.
- Betöltődnek azok a `.z80` fájlok is, amelyek végén az utolsó
  memóriablokk után felesleges adat van (például a SpecDrum
  hangkészlet-pillanatfelvételeinek végén 64 bájt). Eddig ezekre a
  „read_v2_block: not enough data in buffer” hibát kaptuk; a felesleget
  most kihagyja.

### Upstream Fuse

- Az upstream `master` javítása: pillanatfelvétel betöltésekor a joystick
  gombjai (Kempston, Timex, Fuller) elengedett állapotba kerülnek, így nem
  marad beragadva a tűzgomb vagy egy irány (ez az AY lejátszó bezárásakor
  visszaállított programot is érinti).
- Az upstream `master` javítása: az FDI lemezképek olvasása biztonságosabb.
  Az adat nélkülinek jelölt szektorokat (UnrealSpeccy-bővítés) a hibás
  adat-CRC-jű szektoroktól megkülönbözteti, sérült fejléc esetén nem olvas
  a fájl végén túl, és ha egyes szektorok adata túlnyúlik a fájl végén, a
  lemezkép akkor is betöltődik, egy (magyarra fordított) figyelmeztetéssel
  az adat nélkül maradt szektorokról.
- Az upstream `master`-ből: a **+2A/+3 lebegő busza** emulálva van (#360-as
  hiba), így az azt olvasó játékok (például a képernyővel szinkronizálók)
  ezeken a gépeken is futnak.
- Az upstream `master`-ből: az **EmuZWin pillanatfelvételek** (`.ezx`)
  megnyithatók (csak olvasásra), és ott vannak a fájlválasztó szűrőiben.
- Az upstream `master`-ből: az **SDL 2-es** változat akkor is beolvassa a
  billentyűzetet, amikor a Spectrum program a billentyűket olvassa (egy
  képkockában az első olvasáskor, utána legfeljebb 5 ms-onként), nem csak
  képkockánként egyszer, így a billentyűk hamarabb eljutnak a programhoz.
  A Windows-kiosztás szerinti gépelés ezzel is működik. (Most már a
  Windowsos változat is így tesz, lásd fent a *Billentyűzet* részt.)
- Az upstream `master`-ből: a **nyomtatóknak** két kapcsolójuk van a
  *Beállítások > Perifériák > Általános* ablakban: *Soros/párhuzamos
  nyomtató csatlakoztatva* és *ZX Printer csatlakoztatva* (#277-es hiba). A
  +3 párhuzamos portja mindig megvan; a kapcsoló csak azt mondja meg, van-e
  rajta nyomtató.
- Az upstream `master`-ből: az **EDSK lemezképek** (`.dsk`) olvasása
  biztonságosabb, a túl hosszú sávokat a szektorok helyéből építi újra, így
  néhány eddig hibásan betöltődő +3-as lemez is működik (#473-as hiba).
- Az upstream `master`-ből: a DirectSound felszabadítja az objektumát, ha
  nem sikerül elindítani. A hang képkockánál kisebb részekben csak az erre
  felkészített kimenetekre megy; ebben a változatban a DirectSound és a
  WASAPI köztük marad, így a hang időzítése nem változik.

## v1.10.1-hu.1 – 2026-10-30

Mostantól a Fuse 1.10.1-re épül (ennek változásai a [ChangeLog](ChangeLog)
fájlban vannak).

### Hang

- WASAPI hangkimenet (az upstream `feature/wasapi-audio` ágból): a
  `fuse.exe` mostantól ezt használja. Kisebb késleltetés; a hangot egy külön
  szál adja a hangkártyának.
- A WASAPI követi a Windows alapértelmezett hangkimenetét: ha átállítod,
  vagy a használt eszközt kihúzod, a hang a Fuse újraindítása nélkül átvált
  az új alapértelmezett eszközre.
- `fuse-directsound.exe`: ugyanez a program DirectSounddal, tartaléknak. A
  kettő ugyanazokat a beállításokat használja.
- AY sztereó módok: új a **BCA** és a **CAB**; a sorrend most ABC, ACB, BAC,
  BCA, CBA, CAB, Mono, Egyéni. A *Nincs* új neve **Mono** (`--separation
  mono`; a `none` és a *Nincs*-csel elmentett beállítás is működik, és a
  nevek kis- és nagybetűvel is megadhatók).
- Új AY sztereó módok: **CBA** (az ABC tükörképe) és **Egyéni**: a
  *Beállítások > Hang* lapon (vagy a `--ay-channel-a/b/c Left/Centre/Right`
  kapcsolókkal) minden csatornához megadható, hogy balra, középen vagy jobbra
  szóljon, így két csatorna egy oldalon is lehet.
- A *Beállítások > Hang* lap és az AY lejátszó ugyanazokat az AY
  beállításokat mutatja, és azonnal követik egymást (sztereó mód, a
  csatornák helye, AY hangerő); a *Mégse* visszaállítja őket. A csatornák
  helye csak Egyéni módban állítható.

### AY zenelejátszó

- A dalok az emulált géptől függetlenül szólnak: az `.ay` fájl lejátszóját
  egy saját Z80 futtatja (Andre Weissflog chips Z80-a, zlib licenc), saját
  AY hanggenerátorral. A lejátszó már nem veszi át a gépet (nincs +2A,
  nincs pillanatkép): a futó program megy tovább, és a hangja a zenével
  együtt hallatszik. A Szünet csak a zenét állítja meg.
- WASAPI-val (`fuse.exe`) a zenének saját hangfolyama van, amelyet egy
  külön szál táplál: menük, párbeszédablakok és ablakmozgatás alatt is szól,
  miközben az emuláció a szokásos módon szünetel, és ez is követi az
  alapértelmezett hangkimenetet. DirectSounddal a zene képkockánként az
  emulátor hangjába keveredik (300 ms hang van előre, mint eddig).
- A dalok hosszát a program a háttérben, egy külön szálon méri, miközben a
  dal szól (a hossz megtalálásakor a dal nem indul újra); a lista ablak
  *Hosszak mérése* gombja már nem állítja le a szóló dalt. Azokat a hosszakat,
  amelyeknél az előző változat feladta a mérést, újra megméri. A tekerés
  mindkét irányban azonnali.
- A mért hosszakat a program azonnal megtalálja (hash-tábla a memóriában),
  hosszú lejátszási listáknál is; ha egy dal többször szerepel a hosszak
  fájljában, a következő indításkor dalonként egy sorral újraírja a fájlt.
- Pro Tracker 3 modulok (`.pt3`, a Vortex Tracker II-vel készültek is)
  lejátszása: nem fut Z80-as lejátszó, a program képkockánként a modulból
  számolja ki az AY-regisztereket (a lejátszó a ZXTune-é C-ben, képkockánként
  összevetve Sergey Bulba Z80-as lejátszójával és a ZXTune-nal). A cím és a
  szerző a modulból jön; a lista, a hosszmérés, a tekerés, a szintjelzők, a
  sztereó és a csatornanémítás ugyanúgy működik, mint az `.ay` fájloknál. A
  TurboSound moduloknak egyelőre csak az első AY-ja szól.
- Csatornánkénti hangerő: minden szintjelző mellett egy csúszka (0–100%;
  húzás közben a csúszka fölött látszik a százalék, és buborék is mutatja),
  az *Alaphelyzet* gomb mindhármat 100%-ra állítja. A szintjelzők a
  hangerő utáni szintet mutatják. A beállítás megmarad, és a *Beállítások >
  Hang* ablakban is ott van; a két ablak azonnal követi egymást. Az ablak
  kicsit szélesebb lett.
- Szünet alatt a szintjelzők ott maradnak, ahol a zene megállt, és a zenével
  együtt mennek tovább.
- A lejátszási lista ablakban a hosszak mérése leállítható: a *Hosszak
  mérése* gomb mérés közben *Mérés leállítása* lesz (az éppen mért dal
  hosszát még megvárja).
- A mentett lejátszási lista már nem elérhető fájljai (áthelyezve, törölve,
  nem csatlakoztatott meghajtón) hibaüzenet nélkül kimaradnak; egy betöltött
  listánál egyetlen üzenet szól a kimaradt fájlok számáról.

### Egyéb

- A *Súgó > Névjegy* megnyitása nem állítja meg az emulációt (a zene nem
  ugrik meg).

## v1.10.0-hu.1 – 2026-10-25

A változat első kiadása.

### Magyar felület

- Angol vagy magyar felület: *Beállítások > Általános > Nyelv* (vagy
  `--language`). Az *Automatikus* a Windows nyelvét követi. A menük azonnal
  átváltanak; a párbeszédablakok megnyitáskor, az üzenetek megjelenéskor
  kerülnek az új nyelvre.
- Minden menü, beállítás, párbeszédablak és üzenet le van fordítva (a
  fordítást szövegenként átnéztük). A belső, fejlesztőknek szóló üzenetek
  angolul maradnak.
- Ahol a magyar szöveg hosszabb, a párbeszédablakok szélesebbek lesznek:
  feliratok, jelölőnégyzetek, legördülő listák, gombok és maga az ablak; a
  feliratok melletti mezők jobbra tolódnak.
- A legördülő listák elemei, a listák fejlécei, a fájlválasztók címei és a
  fájltípusok is le vannak fordítva.
- A fordítás a `translations/hu.tsv` fájlban van; ebből a
  `translations/make_table.py` készíti az `i18n_hu.c`-t, és közben
  ellenőrzi, hogy minden fordításban ugyanazok a printf-jelölések vannak-e,
  mint az angol szövegben.
- Magyar szóhasználat: *kazetta* a magnókazettára, *Microdrive-kazetta* a
  Microdrive-ra.
- Nyelvváltás után a menüsor elemeit a program újraméri, így a hosszabb
  nevek nem vágódnak le.

### Billentyűzet

- **Gépelés a Windows billentyűkiosztása szerint** (`--windows-layout`,
  alapból bekapcsolva): a gépelés a Windows billentyűkiosztása szerinti
  karaktereket követi, nem a billentyűk helyét, így bármilyen kiosztással
  működik.
  - Kisbetűk és számok: maga a billentyű; nagybetűk: Caps Shift + betű;
    jelek: Symbol Shift + billentyű.
  - A magyar ékezetes magánhangzók ékezet nélkül kerülnek be.
  - A kiterjesztett módot igénylő `[ ] { } \ | ~` és `©` jeleket a program
    időzített billentyűsorozattal gépeli be (a 48K-s és a 128K-s
    szerkesztőben is); a közben leütött billentyűk sorban várakoznak.
  - A Spectrumon nem létező karaktereket figyelmen kívül hagyja.
  - A karakter nélküli billentyűk (kurzor, Enter, funkcióbillentyűk), a Ctrl
    és az Alt, a joystick-billentyűk és a menük a régi módon működnek.
  - A korábbi `--hungarian-keyboard` kapcsolót is elfogadja.
- **A bal Alt legyen Caps Shift a számbillentyűknél** (alapból bekapcsolva):
  a bal Alt + a fő billentyűblokk 0–9 billentyűje Caps Shift + számot ad
  (EDIT, kurzor, DELETE...). Egyébként az Alt megtartja a Windowsban
  megszokott szerepét (menük, Alt+F4); számmal használva utána nem maradnak
  aláhúzva a menübetűk.
- A numerikus billentyűzet (bekapcsolt Num Lock mellett) minden gépelési
  módban számokat és `+ - * / .` jeleket ad; a billentyűi a fizikai
  billentyű szerint engednek fel.
- A más billentyűkön lévő számok (a magyar kiosztás 0-ja) is számok.
- Az AltGr után nem marad lenyomva a Symbol Shift (a Windows az AltGr-t
  Ctrl + Altként küldi).

### Billentyűzetkép (Súgó > Billentyűzet)

- Nagy felbontású kép (`lib/keyboard.jpg`), GDI+-szal dekódolva (JPEG, PNG,
  GIF, BMP), végső tartalékként a `keyboard.scr`.
- Az ablak a Fuse fölött marad, de nem veszi el a fókuszt, így tovább lehet
  gépelni; átméretezhető (negyed méretig), finom kicsinyítéssel, és
  kicsinyítve nyílik meg, ha nem férne el a képernyőn.
- Megjegyzi a helyét és a méretét.

### BASIC programok szövegként

- **Fájl > BASIC szöveg betöltése...**: szövegfájlból olvas be BASIC
  programot, és a memóriában lévő helyére teszi.
  - zmakebas-féle jelölések a blokkgrafikára, a UDG-kre és más kódokra;
    BASin `.bas` fájlok (a fejlécsorokat átugorja, ismeri a `\#nnn`, a
    `` \` `` és a színvezérlő `\{f1}` jelöléseket); UTF-8 vagy Windows-1250
    kódolás.
  - A sorszám nélküli sorok 10, 20, 30... sorszámot kapnak; az ékezetes
    betűket kicseréli, és ezt jelzi is.
  - 48K-s BASIC-ben a megerősítő üzenet bezárása után kilistázza a
    programot; a 128K-s szerkesztőből kilép és visszalép, így az az új
    programot listázza.
  - Ha épp fut egy program, rákérdez, és előbb újraindítja a gépet.
- **Fájl > BASIC mentése szövegként...**: a memóriában lévő programot
  szövegként menti.

### AY zenelejátszó

- `.ay` (ZXAYEMUL) zenefájlokat játszik le egy Spectrum +2A-n, csupa-RAM
  különleges lapozással és a formátum leírása szerinti lejátszóprogrammal.
  A dalokat a ZEsarUX módján indítja (regiszterek, lejátszóprogram, blokkok);
  az indító rutin nélküli dalok a leírás szerint az első blokkjukat hívják
  meg (a ZEsarUX ott indította a processzort, ezért ezek a dalok némák
  maradtak).
- **Megnyitás**: a *Média > AY zenelejátszó...* lejátszás nélkül nyitja meg;
  a *Fájl > Megnyitás*, a fájl Fuse-ra húzása vagy a parancssor a fájl dalait
  a lejátszási lista végére teszi, és az elsőt elindítja.
- **A futó program megmarad**: az első lejátszott dal előtt a Fuse
  pillanatképet készít a gépről; a lejátszó vagy a Fuse bezárásakor
  visszaáll, így a kilépéskor mentett munkamenet is azt őrzi meg.
- **Lejátszóablak**: fájladatok, az éppen szóló dal, eltelt idő és hossz,
  csúszka a tekeréshez (előre az emuláció hang, kirajzolás és várakozás
  nélkül fut, visszafelé a dal újraindul), Előző / Szünet (valódi szünet) /
  Leállítás / Következő, Lista.
- **Lejátszás**: *1 dal* vagy *Teljes lista*, valamint *Ismétlés*:
  - 1 dal: a végén megáll; Ismétléssel újra a dal (a körbeérő zenéjű dal
    egyszerűen szól tovább, az eltelt idő visszaugrik a kör kezdetére).
  - Teljes lista: a következő dal; a lista végén megáll, vagy Ismétléssel
    újra az első dal.
  - A beállításokban megmarad.
- **Lejátszási lista ablak**: akárhány fájl dalai; Hozzáadás (több fájl),
  Mappa hozzáadása, Eltávolítás (a Delete billentyűvel is), Lista törlése,
  Fel / Le, Lista betöltése és mentése (`.m3u`, `útvonal::AY,dal` sorokkal a
  NEZplug módján, a puszta útvonal a fájl összes dalát hozzáadja), Hosszak
  mérése, fájlok, mappák vagy listák behúzása az Intézőből; dupla kattintás
  vagy Enter: lejátszás; a szóló dal félkövér. Átméretezhető (a lista
  követi, a gombok mérete marad), megjegyzi a helyét, méretét és az oszlopok
  szélességét. A lista a `fuse-ay-playlist.m3u` fájlban marad meg.
- **A dalok hosszát a program megméri**: egy dal első lejátszásakor a Fuse
  hang, várakozás és kirajzolás nélkül futtatja a dalt, és képkockánként
  feljegyzi az AY regisztereit, amíg a zene ismétlődni nem kezd (a
  legrövidebb, legalább egy percig ismétlődő periódus, majd vissza az
  ismétlődés kezdetéig). A dal az első kör végéig tart; ha csend ismétlődik,
  a zene végéig. Legfeljebb 15 másodperc; a nem ismétlődő dalok 5 percet
  kapnak. A mérést egy sor jelzi az ablak alján. A hosszakat és a körbeérés
  helyét a fájl ellenőrzőösszege szerint a `fuse_ay_lengths.txt` őrzi, és
  ezek felülírják a fájlokban megadott hosszt („(mért)”,
  „(alapértelmezett)”).
- A *Hosszak mérése* a lista még meg nem mért dalait méri meg, lejátszás
  közben is (utána a dal ott folytatódik, ahol abbamaradt).
- **Elhalkulás**: a körbeérő dal 3 másodperc alatt elhalkul, amikor a
  lejátszó elhagyja (az 5 perces alapértékű dal 5 másodperc alatt); a
  magától véget érő dalok nem halkulnak el.
- **Szintjelzők** a három AY-csatornára (a burkolóval együtt), a sztereó mód
  sorrendjében, csatornánként némító jelölőnégyzettel (a szintjelző szürkén
  mozog tovább; a lejátszó bezárásakor minden csatorna újra bekapcsol).
- **Hangerő** és **sztereó mód** a lejátszóablakban, lejátszás közben
  azonnal érvényesül.
- A zene akkor is szól tovább, ha egy menü nyitva van, ha egy ablakot
  mozgatsz vagy átméretezel (már a kattintástól), ha egy lista lenyílik, és ha
  a Fuse kis méretre van állítva: egy időzítő futtatja a képkockákat, amikor
  a fő ciklus elakad, és lejátszás közben a hangkimenet kb. 300 ms-mal előre
  dolgozik.

### Hang

- Új AY sztereó mód: **BAC** (Beállítások > Hang, `--separation bac`).
- Az AY hangerő és a sztereó mód lejátszás közben, a hang újraindítása
  nélkül változtatható (a hangerő az amplitúdókat skálázza, a sztereó módok
  közti váltás csak a csatornákat köti át).
- DirectSound:
  - akadás után nem játszik le újra régi hangot (a billentyűkattanások
    kétszer hallatszottak);
  - mindig az írókurzoron túlra ír; akadáskor kb. 2 ms alatt elhalkul, és nem
    tartja az utolsó mintát;
  - egymásodperces puffer: a játékoknál marad a kis késleltetés, az AY
    lejátszó kb. 300 ms-mal előre dolgozik.
- Kikapcsolt hang mellett nem gyűlnek AY-regiszterírások, és a hang
  újraindulásakor az AY hangkeltő az AY aktuális regisztereiből indul (korábban
  a hang kikapcsolása után hangok ragadhattak be).

### Ablak és beállítások

- **Teljes képernyő** (F11, *Beállítások > Teljes képernyő*,
  `--full-screen`): keret nélküli, egész számú nagyítású, középre igazított, a
  [jfroco/fuse](https://github.com/jfroco/fuse) változatból átvéve;
  monitoronkénti DPI-kezeléssel. Az SDL 2-es változat is megkapta az F11-et.
- **Folytatás ott, ahol abbahagytam** (*Beállítások > Általános*,
  `--resume-last-session`): kilépéskor a Fuse elmenti a gépet a betett
  kazettával és annak állásával, és a következő indításkor visszaállítja
  (hacsak nincs megnyitandó fájl megadva), a kiválasztott perifériákkal együtt.
- **Rákérdezés kilépéskor** (alapból bekapcsolva; szürke, ha a megerősítések ki
  vannak kapcsolva).
- A perifériák beállításainál szürkék azok, amelyek az emulált gépen nem
  lehetnek.
- A menüsort a Fuse rajzolja: a kijelölt elem sötétebb szürke, billentyűzetes
  használatkor a hívóbetűkkel.
- Az információs üzenetek Windows-hang nélkül jelennek meg.
- A főablak, a billentyűzetkép és az AY lejátszó ablakai ott nyílnak meg, ahol
  legutóbb voltak (ha az még képernyőn van).
- Minden, amit a Fuse megőriz, a `%APPDATA%\Fuse` mappában van: `fuse.cfg`,
  `fuse-windows.ini` (az ablakok helye), `fuse-ay-playlist.m3u`,
  `fuse_ay_lengths.txt` és az utolsó munkamenet. A korábbi verziók fájljait a
  program az első indításkor ide költözteti.

### Fordítás

- `build-windows-cross.sh`: a Windowsos Fuse-t (Win32 felület, DirectSound)
  Linuxról vagy WSL-ből fordítja MinGW-w64-gyel, a libspectrumot statikus
  könyvtárként, és a DLL-ekkel, ROM-okkal és adatfájlokkal együtt
  becsomagolja.
- VS Code-beállítások a WSL-ből való fordításhoz (Ctrl+Shift+B), a program
  indításához és az IntelliSense-hez a MinGW-w64-es fordításhoz.
