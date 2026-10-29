# A magyar Windows-kiadás változásai

[English](CHANGELOG-fork.md) | **Magyar**

Ennek a változatnak a módosításai a Fuse 1.10.0-hoz képest. Magának a
Fuse-nak a változásai a [ChangeLog](ChangeLog) fájlban vannak (angolul).

## Kiadatlan

### Hang

- WASAPI hangkimenet (az upstream `feature/wasapi-audio` ágból): a
  `fuse.exe` mostantól ezt használja. Kisebb késleltetés; a hangot egy külön
  szál adja a hangkártyának; amíg az AY lejátszó szól, 300 ms hang van
  előre.
- `fuse-directsound.exe`: ugyanez a program DirectSounddal, tartaléknak. A
  kettő ugyanazokat a beállításokat használja.

### AY zenelejátszó

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
