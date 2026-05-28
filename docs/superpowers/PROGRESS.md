# MPPatcher — Progresso e Contexto
**Última atualização:** 2026-05-28
**Branch:** `codex/body-hand-normal-routing`

---

## O que estamos construindo

**MPPatcher** — ferramenta para adicionar suporte ao ModernPhysiquePicker (MPP) em outfits BodySlide existentes.

Dois artefatos:
1. **MPPatcher** (público, distribuível no Nexus) — GUI simples: seleciona SliderGroup, clica Patch
2. **migrate_legacy.py** (interno) — migra patches do mod antigo "Fat People of Skyrim" para o novo sistema

---

## Sliders MPP novos
- `RM_Heavy`
- `RM_Muscular`
- `RM_Thin`

## Reference bodies MPP

| Tipo | Feminino (CBBE) | Masculino (HIMBO) |
|---|---|---|
| Body | `Rley COR - CBBE Body` | `Rley HIMBO BODY 2` |
| Hands | `Rley COR - CBBE Hands` | `Rley HIMBO Hands` |
| Feet | `Rley COR - CBBE Feet` | `Rley HIMBO Feet` |

## Sliders legados (a migrar → RM_Heavy)

| Gênero | Slider legado |
|---|---|
| Feminino | `Chubby Refit` |
| Feminino | `Rley's CBBE Outfit` (com apóstrofo) |
| Feminino | `Rley CBBE Shoulder` + `Rley CBBE Outfit` (sem apóstrofo — coalesce pair) |
| Masculino | `Fat Morph` |
| Masculino | `Fat Morphs` (typo) |

## Fonte dos patches legados
`E:\Sculpting\00000 PC Ver 2\Packages\`
- `FOS - Vanilla Outfits 3BA Refit.osp` — 210 SliderSets (feminino vanilla)
- `FOS HIMBO Vanilla Refits.osp` — 299 SliderSets (masculino vanilla)
- `FOS 3BA Sons of Skyrim Refit.osp` — 99 SliderSets
- `FOS 3BA Realistic Armor Refit.osp` — 42 SliderSets
- `FOS CBBE Body.osp` — body reference (NÃO patchar)
- `Rley COR HIMBO.osp` — body reference (NÃO patchar)

Total: ~650+ SliderSets

---

## Decisão de arquitetura (FINAL — audit completo 2026-05-28)

### ❌ Plano original: fork C++ do BodySlide source
### ✅ Plano atual: usar o modo headless nativo do OutfitStudio

**Descoberta crítica do audit (2026-05-28):**
O OutfitStudio já tem um sistema de automação headless completo:
```
OutfitStudio.exe --automation script.xml
```
- 39 step types: `LoadReference`, `ClearReference`, `ConformSliders`, `DeleteSlider`, `SaveProject`, etc.
- Batch mode `SliderSets` — processa todos os SliderSets de uma pasta automaticamente
- `ConformSliders` aceita `SliderNames` para conformar apenas sliders específicos
- `DeleteSlider` tem suporte a regex

**MPPatcher.exe = UI própria (Qt5) + chama OutfitStudio como engine headless.**

O MPPatcher tem sua própria janela, seu próprio design, sua própria branding. O que mudou é que ele NÃO precisa reimplementar o Conform All — ele gera um XML de automação e chama `OutfitStudio.exe -automation <nome>`. O OutfitStudio roda sem janela e faz o trabalho pesado.

**Fluxo:**
1. Usuário abre MPPatcher.exe
2. Seleciona SliderGroups, modo de output, etc.
3. MPPatcher gera o XML e salva em `<OS data>/Automations/mppatcher_run.xml`
4. Chama `OutfitStudio.exe -automation mppatcher_run` como subprocess
5. Monitora exit code e mostra resultado

**Limitação confirmada:** Não existe `RenameSlider` no sistema de automação. Para `Chubby Refit → RM_Heavy`, o Python ainda é necessário para renomear entradas no OSD binário. O OSD é simples (sem compressão/checksum) e o Python scope é mínimo.

---

## Estado atual das tarefas

### Fase de planejamento (concluída)
- [x] Design spec: `docs/superpowers/specs/2026-05-28-mppatcher-design.md`
- [x] Plano Phase 1 (desatualizado, veja abaixo): `docs/superpowers/plans/2026-05-28-mppatcher-phase1-cli.md`
- [x] Audit BS source: `docs/superpowers/audit-bs-source.md`

### Próximos passos

**⚠️ O plano Phase 1 (CLI C++) está desatualizado.** Audit completo — arquitetura confirmada.

O que está resolvido após o audit:

- OSD format: confirmado, trivial de parsear em Python
- ClearReference vs SetReferenceShape: comportamentos distintos, ambos mapeados
- Command line: `OutfitStudio.exe -automation <nome>`, script deve estar em `<ProjectPath>/Automations/`
- Engine: não precisa reimplementar nada — OutfitStudio roda headless

**Próximo passo imediato:** Escrever novo plano de implementação com a arquitetura correta:

- Plano A: MPPatcher Qt5 (C++) — UI própria, chama OS como subprocess
- Plano B: migrate_legacy.py — Python OSD rename + automation XML para migração interna

---

## Arquivos chave

| Arquivo | Descrição |
|---|---|
| `E:\SkyrimDev\BodySlide-OS-Fork\src\components\Automation.h` | Definição dos 39 step types |
| `E:\SkyrimDev\BodySlide-OS-Fork\src\components\Automation.cpp` | Parser XML do automation script |
| `E:\SkyrimDev\BodySlide-OS-Fork\src\program\AutomationDialogExecute.cpp` | Implementação de cada step |
| `E:\SkyrimDev\BodySlide-OS-Fork\src\program\OutfitProject.cpp` | SaveSliderData / OSD format |
| `E:\SkyrimDev\BodySlide-OS-Fork\src\program\OutfitStudio.cpp` | OnInit, cmdAutomation, headless mode |
| `E:\SkyrimDev\RleyBodyMorphs\docs\superpowers\specs\2026-05-28-mppatcher-design.md` | Design spec |
| `E:\SkyrimDev\RleyBodyMorphs\docs\superpowers\audit-bs-source.md` | Audit completo |
| `E:\Sculpting\00000 PC Ver 2\Packages\` | Patches legados (fonte para migração) |

---

## Regra de referência (universal)
- Outfit **tinha** reference → remove, coloca MPP reference, mantém MPP reference no final
- Outfit **não tinha** reference → adiciona MPP reference temporariamente, faz conform, remove antes de salvar
- **Sempre manter o estado que estava**

## Repo do patcher
Separado do MPP: `E:\SkyrimDev\BodySlide-OS-Fork\` (futuramente repo próprio)
