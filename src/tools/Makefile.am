EXTRA_DIST = \
	README.md \
	__init__.py \
	matfu.py \
	unpak.py \
	mdl2obj.py \
	md22obj.py \
	md32obj.py \
	objfu.py \
	pyproject.toml \
	run_pty_commands.py \
	skyfu.py \
	symbolicate_dmp.py \
	test_verify_race_overrides.py \
	verify_pristine_tree.py \
	verify_projects.py \
	verify_race_overrides.py \
	verify_race_ui.py

.PHONY: install-tools
install-tools:
	python3 -m venv .venv
	.venv/bin/python3 -m pip install numpy pillow opencv-python scikit-image scipy
	.venv/bin/python3 -m pip install -e .
	@echo "Tools installed. Activate with: source src/tools/.venv/bin/activate"
