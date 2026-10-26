# Project instructions

- Unless explicitly told not to, wait for answers to questions before proceeding with work that depends on those answers.
- Establish the intended feature branch before beginning implementation. Use the branch specified by the owner and keep subsequent work on that branch.
- Use and reuse GPT-6 Luna subagents with **max** reasoning effort as appropriate to the task. Delegate bounded grunt work, including mechanical edits, repetitive validation, inventories, and documentation checks. Give each delegated task explicit scope and file ownership.
- Use multiple subagents in parallel when tasks are sufficiently independent and parallel work will not degrade quality. Keep their file ownership and generated outputs separate; the primary agent reviews the combined result.
- The primary agent retains architecture, scope decisions, evidence assessment, final review, and Git topology. Review delegated work before integrating or reporting it as complete.
- Coordinate shared outputs: do not run concurrent PlatformIO commands against the same build tree or allow overlapping edits to the same files. Include the owner's build/upload operations in that coordination; after handing images over, keep the shared build tree idle while the owner flashes.
- Changes affecting boot, GATT registration, or advertising require executable deterministic successful-startup and injected-failure tests. Exercise production orchestration and validate adapter contracts against the pinned stack, including distinct characteristic-property and descriptor-permission domains. Keep build/static checks and physical acceptance separate.
