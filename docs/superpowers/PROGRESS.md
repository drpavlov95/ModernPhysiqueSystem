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

## Decisão de arquitetura (ATUALIZADA após audit)

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

**Novo fluxo público (para outros modders):**
```xml
<AutomationScript>
    <Batch mode="SliderSets">...</Batch>
    <Step type="ClearReference" />
    <Step type="LoadReference">...</Step>
    <Step type="ConformSliders">
        <SliderNames>RM_Heavy,RM_Muscular,RM_Thin</SliderNames>
    </Step>
    <Step type="SaveProject" />
</AutomationScript>
```

**MPPatcher** = gera esse XML + chama OutfitStudio headlessly. Muito mais simples que um fork C++.

**Limitação descoberta:** Não existe `RenameSlider` ou `CopySlider` no sistema de automação. Para renomear `Chubby Refit → RM_Heavy`, ainda é necessário manipular o arquivo `.osd` externamente (Python). Mas o escopo do Python é bem menor do que o planejado originalmente.

---

## Estado atual das tarefas

### Fase de planejamento (concluída)
- [x] Design spec: `docs/superpowers/specs/2026-05-28-mppatcher-design.md`
- [x] Plano Phase 1 (desatualizado, veja abaixo): `docs/superpowers/plans/2026-05-28-mppatcher-phase1-cli.md`
- [x] Audit BS source: `docs/superpowers/audit-bs-source.md`

### Próximos passos

**⚠️ O plano Phase 1 (CLI C++) está desatualizado.** A descoberta do modo headless muda tudo.

Antes de escrever código, precisa:

1. **Verificar OSD binary format** — ler `OutfitProject.cpp` linhas 390-450 para entender o formato binário antes de escrever o parser Python
2. **Verificar comportamento do SetReferenceShape** — como funciona o caso "sem reference" (`AutomationDialogExecute.cpp` linhas 530-600)
3. **Escrever novo plano** baseado na arquitetura de automation XML:
   - MPPatcher GUI (Python/Qt ou similar) gera automation XML
   - Chama `OutfitStudio.exe --automation`
   - migrate_legacy.py (Python) faz só o rename no OSD + re-import

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
