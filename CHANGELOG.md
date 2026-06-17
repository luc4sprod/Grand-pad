# Changelog — Grand-Pad

Histórico de mudanças do firmware. Formato livre, em português, pensado para
acompanhar commits no GitHub.

---

## v2.7

### Adicionado
- Botão coringa (C7): envia `CC_WILDCARD` (CC 30, configurável via `#define`),
  alternando 127 (pressionado) / 0 (solto) — função livre para mapear na DAW.

### Alterado
- Tela de performance reestruturada: zona dedicada para exibição do nome do
  acorde ativo (y=29–45), separada por linha horizontal da linha Root/Escala,
  eliminando a sobreposição entre "Esc:" e o nome do acorde em destaque.
- Linha de rodapé ("C1-3:modo C5/6:oct MENU") removida — estava cortada na
  tela e o espaço foi redistribuído entre as demais linhas.
- Label "Sustain Fix" no menu Config renomeado para "Sustain" e padding
  ajustado (`%-12s`) para garantir espaço antes do valor.

### Removido
- Função Play/Pause DAW (CC 29) e variável `dawPlaying`.

### Corrigido
- Bug visual no menu Config: "Sustain Fix" + "OFF" colavam sem espaço
  (`Sustain FixOFF`), exibindo um caractere estranho ("X") entre as palavras.

### Notas técnicas
- `FLASH_MAGIC` atualizado para `'MIDT'` — presets salvos em versões
  anteriores serão resetados para os valores padrão na primeira gravação.

---

## v2.6

### Removido
- Integração completa com M5StickC: ponte BLE-MIDI, protocolo UART
  bidirecional, comandos `0xF5`/pacotes `0xFE`, e modo Afinador Cromático
  (YIN) com tela dedicada no OLED.

### Alterado
- Pads restaurados para 16 unidades (GP0–GP15), liberando GP0/GP1 que
  estavam reservados para UART.
- C7 voltou a ser exclusivamente Play/Pause DAW (CC 29), sem detecção de
  hold longo.
- Nomes dos modos abreviados no display ("Chromat", "Generat", "OmniChrd")
  para melhor encaixe em fonte tamanho 2.

### Notas técnicas
- `FLASH_MAGIC` atualizado para `'MIDS'`.

---

## v2.5

### Adicionado
- Exibição do nome do acorde/nota ativa no OLED ao pressionar pads nos
  modos Acorde e Harpejo (ex: "C", "Am", "G7", "Bdim", "D (baixo)").
- Tabela `CHORD_QUALITY[8][8]` com a qualidade harmônica de cada grau
  para cada um dos 8 modos de acorde.

### Notas técnicas
- `FLASH_MAGIC` atualizado para `'MIDR'`.

---

## v2.4

### Adicionado
- Integração M5StickC via UART bidirecional (GP0=TX, GP1=RX, 31250 baud).
- Ponte BLE-MIDI (M5StickC retransmite MIDI recebido via UART para BLE).
- Modo Afinador Cromático com algoritmo YIN no M5StickC, tela dedicada
  no OLED do Grand-Pad com indicador de cents.
- C7 com dupla função: toque curto = Play/Pause DAW, hold ≥1s = toggle
  do afinador.

### Alterado
- Pads reduzidos para 14 (GP2–GP15) para liberar GP0/GP1 para UART.

### Notas técnicas
- `FLASH_MAGIC` atualizado para `'MIDQ'`.

---

## v2.3

### Adicionado
- Modo Harpejo (Arpegio): cada pad dispara um arpejo ascendente do voicing
  de acorde correspondente, com BPM e divisão configuráveis no menu Config.
- Remapeamento completo dos botões C1–C7 em modo performance:
  - C1 → Modo Cromático
  - C2 → Modo Acorde
  - C3 → Modo Harpejo
  - C4 → Sustain touch (hold = CC64 127, release = CC64 0)
  - C5 → Oitava +1
  - C6 → Oitava −1
  - C7 → Play/Pause DAW (CC 29)
- Nova tela de performance: modo, oitava (notação piano), root note,
  escala, acorde, velocidade, transpose, canal e estado de sustain.

---

## v2.2

### Adicionado
- Reestruturação dos botões do menu: C1=Cursor▲, C2=Cursor▼, C3=Ajuste−,
  C4=Ajuste+/Confirmar, C5=Próxima página, C6=Back/Fechar, C7=Salvar preset.
- Opção de Sustain Fixo (toggle) na página Config do menu.
- Página de Presets (5ª página do menu) com 4 slots.

### Alterado
- Otimizações de eficiência geral e ajuste de posicionamento dos itens
  no display OLED.

---

## v2.1 (base)

Versão inicial do fork — macropad MIDI de 16 pads + 7 botões de controle,
com modos Scaled, Chromatic, Chord, Generative e OmniChord, menu de
configuração via OLED SSD1306, e presets salvos em flash.

Fork de [Macropad MIDI Controller](https://github.com/NickCulbertson/Macropad-MIDI-Controller)
por Nick Culbertson.
