# Агенты

Промпты агентов. Один файл — один агент (задача T-021…T-040 в CRM).

| Агент | Файл | Пишет в CRM |
|---|---|---|
| Scout | `scout.md` | Leads (`new`) |
| Qualifier | `qualifier.md` | Leads (`score`, `qualified`/`disqualified`) |
| Outreach | `outreach.md` | Outreach, Leads (`contacted`…`consent_yes`) |
| Researcher | `researcher.md` | Projects (`brief_url`, `brief_fill_pct`) |
| Builder | `builder.md` | Projects (`preview_url`, `cost_usd`, `time_min`) |
| QA | `qa.md` | Projects (`qa_status`, `lh_*`) |
| Closer | `closer.md` | Projects, Clients, Outreach |
