# Poprawki PO — przegląd podsumowania funkcjonalności zBox

## Punkt 5: Zasilanie i sen

**Uzupełnienia:**
- **Włączenie:** towarzyszy mu animacja bootowania (niebieski pasek postępu — jak w obecnym kodzie)
- **Wyłączenie normalne:** użytkownik przytrzymuje C → po 2 sekundach zaczyna migać czerwona animacja → jeśli użytkownik **puści przycisk w trakcie czerwonego migania** (przed 10 sekundami) → odpala się animacja wyłączenia i urządzenie zasypia
- **Wyłączenie awaryjne (emergency sleep):** użytkownik trzyma C dalej aż do 10 sekund → natychmiastowe wyłączenie bez pełnej sekwencji

---

## Punkt 3: Lampka nocna (Light Mode)

**Uzupełnienia:**
- Timer bezczynności wynosi **15 minut**
- Wciśnięcie C lub D (regulacja jasności) **resetuje timer** — liczenie zaczyna się od nowa

---

## Punkt 1: Odtwarzanie kart NFC (Card Mode)

**Błędy w opisie:**
- Nie ma figurek — są tylko karty NFC
- Mechanika działania: użytkownik **wkłada kartę** → muzyka gra; **wyciąga kartę** → muzyka się zatrzymuje (nie "kolejne przyłożenie")
- Gdy karta jest w slocie i utwór się kończy → muzyka **nie wznawia się automatycznie**; użytkownik musi wyjąć kartę i włożyć ponownie

