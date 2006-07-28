; #######################################
; german.nsh
; german language strings for inkscape installer
; windows code page: 1252
; Authors:
; Adib Taraben theAdib@yahoo.com
;
!insertmacro MUI_LANGUAGE "German"

; Product name
LangString lng_Caption   ${LANG_GERMAN}  "${PRODUCT_NAME} -- Open Source SVG-Vektorillustrator"

; Button text "Next >" for the license page
LangString lng_LICENSE_BUTTON   ${LANG_GERMAN} "Weiter >"

; Bottom text for the license page
LangString lng_LICENSE_BOTTOM_TEXT   ${LANG_GERMAN} "$(^Name) wird unter der GNU General Public License (GPL) ver�ffentlicht. Die Lizenz dient hier nur der Information. $_CLICK"

;has been installed by different user
LangString lng_DIFFERENT_USER ${LANG_GERMAN} "Inkscape wurde durch den Benutzer $0 installiert.$\r$\nWenn Sie fortfahren kann die Aktion m�glicherweise nicht korrekt abgeschlossen werden!$\r$\nBitte melden Sie sich als $0 an und versuchen Sie es erneut."

;you have no admin rigths
LangString lng_NO_ADMIN ${LANG_GERMAN} "Sie sind nicht Computeradministrator.$\r$\nDas Installieren f�r alle Benutzer kann m�glicherweise nicht korrekt abgeschlossen werden.$\r$\nBitte deselektieren Sie die Option 'f�r Alle Benutzer'."

;win9x is not supported
LangString lng_NOT_SUPPORTED ${LANG_GERMAN} "Es ist bekannt, dass Inkscape unter Windows 95/98/ME nicht oder nicht stabil l�uft!$\r$\nBitte pr�fen Sie die offizielle Webseite f�r detaillierte Informationen."

; Full install type
LangString lng_Full $(LANG_GERMAN) "Komplett"

; Optimal install type
LangString lng_Optimal $(LANG_GERMAN) "Optimal"

; Minimal install type
LangString lng_Minimal $(LANG_GERMAN) "Minimal"


; Core section
LangString lng_Core $(LANG_GERMAN) "${PRODUCT_NAME} Vektorillustrator (erforderlich)"

; Core section description
LangString lng_CoreDesc $(LANG_GERMAN) "${PRODUCT_NAME} Basis-Dateien und -DLLs"


; GTK+ section
LangString lng_GTKFiles $(LANG_GERMAN) "GTK+ Runtime Umgebung (erforderlich)"

; GTK+ section description
LangString lng_GTKFilesDesc $(LANG_GERMAN) "Ein Multi-Plattform GUI Toolkit, verwendet von ${PRODUCT_NAME}"


; shortcuts section
LangString lng_Shortcuts $(LANG_GERMAN) "Verkn�pfungen"

; shortcuts section description
LangString lng_ShortcutsDesc $(LANG_GERMAN) "Verkn�pfungen zum Start von ${PRODUCT_NAME}"

; multi user installation
LangString lng_Alluser  ${LANG_GERMAN}  "f�r Alle Benutzer"

; multi user installation description
LangString lng_AlluserDesc  ${LANG_GERMAN}  "Installiert diese Anwendung f�r alle Benutzer dieses Computers (all users)"

; Start Menu  section
LangString lng_Startmenu $(LANG_GERMAN) "Startmen�"

; Start Menu section description
LangString lng_StartmenuDesc $(LANG_GERMAN) "Erstellt einen Eintrag f�r ${PRODUCT_NAME} im Startmen�"

; Desktop section
LangString lng_Desktop $(LANG_GERMAN) "Desktop"

; Desktop section description
LangString lng_DesktopDesc $(LANG_GERMAN) "Erstellt eine Verkn�pfung zu ${PRODUCT_NAME} auf dem Desktop"

; Quick launch section
LangString lng_Quicklaunch $(LANG_GERMAN) "Schnellstartleiste"

; Quick launch section description
LangString lng_QuicklaunchDesc $(LANG_GERMAN) "Erstellt eine Verkn�pfung zu ${PRODUCT_NAME} auf der Schnellstartleiste"

; File type association for editing
LangString lng_SVGWriter    ${LANG_GERMAN}  "�ffne SVG Dateien mit ${PRODUCT_NAME}"

;LangString lng_UseAs ${LANG_ENGLISH} "Select ${PRODUCT_NAME} as default application for:"
LangString lng_SVGWriterDesc    ${LANG_GERMAN}  "W�hlen Sie ${PRODUCT_NAME} als Standardanwendung f�r SVG Dateien"

; Context Menu
LangString lng_ContextMenu ${LANG_GERMAN} "Kontext-Men�"

; Context Menu description
LangString lng_ContextMenuDesc ${LANG_GERMAN} "F�gt ${PRODUCT_NAME} in das Kontext-Men� f�r SVG Dateien ein"


; Additional Files section
LangString lng_Addfiles $(LANG_GERMAN) "weitere Dateien"

; additional files section dscription
LangString lng_AddfilesDesc $(LANG_GERMAN) "weitere Dateien"

; Examples section
LangString lng_Examples $(LANG_GERMAN) "Beispiele"

; Examples section dscription
LangString lng_ExamplesDesc $(LANG_GERMAN) "Beispiele mit ${PRODUCT_NAME}"

; Tutorials section
LangString lng_Tutorials $(LANG_GERMAN) "Tutorials"

; Tutorials section dscription
LangString lng_TutorialsDesc $(LANG_GERMAN) "Tutorials f�r die Benutzung mit ${PRODUCT_NAME}"


; Languages section
LangString lng_Languages $(LANG_GERMAN) "�bersetzungen"

; Languages section dscription
LangString lng_LanguagesDesc $(LANG_GERMAN) "Installiert verschiedene �bersetzungen f�r ${PRODUCT_NAME}"

LangString lng_am $(LANG_GERMAN) "am  Amharisch"
LangString lng_az $(LANG_GERMAN) "az  Aserbaidschanisch"
LangString lng_be $(LANG_GERMAN) "be  Wei�russisch"
LangString lng_ca $(LANG_GERMAN) "ca  Katalanisch"
LangString lng_cs $(LANG_GERMAN) "cs  Tschechisch"
LangString lng_da $(LANG_GERMAN) "da  D�nisch"
LangString lng_de $(LANG_GERMAN) "de  Deutsch"
LangString lng_el $(LANG_GERMAN) "el  Griechisch"
LangString lng_en $(LANG_GERMAN) "en  Englisch"
LangString lng_en_CA $(LANG_GERMAN) "en_CA  Englisch, wie in Kanada gesprochen"
LangString lng_en_GB $(LANG_GERMAN) "en_GB  Englisch, wie in Gro�britannien gesprochen"
LangString lng_es $(LANG_GERMAN) "es  Spanisch"
LangString lng_es_MX $(LANG_GERMAN) "es_MX  Spanisch-Mexio"
LangString lng_et $(LANG_GERMAN) "et  Estonisch"
LangString lng_fi $(LANG_GERMAN) "fi  Finnisch"
LangString lng_fr $(LANG_GERMAN) "fr  Franz�sisch"
LangString lng_ga $(LANG_GERMAN) "ga  Irisch"
LangString lng_gl $(LANG_GERMAN) "gl  Galizisch"
LangString lng_hr $(LANG_GERMAN) "hr  Kroatisch"
LangString lng_hu $(LANG_GERMAN) "hu  Ungarisch"
LangString lng_it $(LANG_GERMAN) "it  Italienisch"
LangString lng_ja $(LANG_GERMAN) "ja  Japanisch"
LangString lng_ko $(LANG_GERMAN) "ko  Koreanisch"
LangString lng_lt $(LANG_GERMAN) "lt  Litauisch"
LangString lng_mn $(LANG_GERMAN) "mn  Mongolisch"
LangString lng_mk $(LANG_GERMAN) "mk  Mazedonisch"
LangString lng_nb $(LANG_GERMAN) "nb  Norwegisch-Bokmal"
LangString lng_nl $(LANG_GERMAN) "nl  Holl�ndisch"
LangString lng_nn $(LANG_GERMAN) "nn  Nynorsk-Norwegisch"
LangString lng_pa $(LANG_GERMAN) "pa  Panjabi"
LangString lng_pl $(LANG_GERMAN) "po  Polnisch"
LangString lng_pt $(LANG_GERMAN) "pt  Portugiesisch"
LangString lng_pt_BR $(LANG_GERMAN) "pt_BR  Portugiesisch Brazilien"
LangString lng_ru $(LANG_GERMAN) "ru  Russisch"
LangString lng_rw $(LANG_GERMAN) "rw  Kinyarwanda"
LangString lng_sk $(LANG_GERMAN) "sk  Slowakisch"
LangString lng_sl $(LANG_GERMAN) "sl  Slowenisch"
LangString lng_sq $(LANG_GERMAN) "sq  Albanisch"
LangString lng_sr $(LANG_GERMAN) "sr  Serbisch"
LangString lng_sr@Latn $(LANG_GERMAN) "sr@Latn Serbisch mit lat. Buchstaben"
LangString lng_sv $(LANG_GERMAN) "sv  Schwedisch"
LangString lng_tr $(LANG_GERMAN) "tr  T�rkisch"
LangString lng_uk $(LANG_GERMAN) "uk  Ukrainisch"
LangString lng_vi $(LANG_GERMAN) "vi  Vietnamesisch"
LangString lng_zh_CN $(LANG_GERMAN) "zh_CH  Chinesisch (vereinfacht)"
LangString lng_zh_TW $(LANG_GERMAN) "zh_TW  Chinesisch (traditionell)"


; uninstallation options
;LangString lng_UInstOpt   ${LANG_ENGLISH} "Uninstallation Options"
LangString lng_UInstOpt   ${LANG_GERMAN} "Deinstallations Optionen"

; uninstallation options subtitle
;LangString lng_UInstOpt1  ${LANG_ENGLISH} "Please make your choices for additional options"
LangString lng_UInstOpt1  ${LANG_GERMAN} "Bitte w�hlen Sie die optionalen Deinstalltionsparameter"

; Ask to purge the personal preferences
;LangString lng_PurgePrefs ${LANG_ENGLISH} "Keep Inkscape preferences"
LangString lng_PurgePrefs ${LANG_GERMAN}  "behalte pers�nliche Inkscape-Vorgaben"
