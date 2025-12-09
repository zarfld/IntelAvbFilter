# Documentation Generation Templates Guide

**Purpose**: This guide explains how to use the included documentation generation templates for your project.

**Standards**: ISO/IEC/IEEE 12207:2017 (Transition Process - Documentation)

## 📚 Overview

This template repository includes generic documentation generation workflows that can be customized for various project types:

- **Doxygen** (C/C++, Python, Java) - API documentation from code comments
- **MkDocs** (Python/Markdown) - Static site from Markdown files
- **Sphinx** (Python) - Documentation with autodoc
- **JSDoc** (JavaScript/TypeScript) - JavaScript API docs
- **Javadoc** (Java) - Java API documentation

## 🎯 What's Included

### 1. GitHub Actions Workflow (`.github/workflows/documentation.yml`)

**Features**:
- Automatic documentation generation on push/PR
- GitHub Pages deployment
- Documentation artifacts for PRs
- Coverage reporting
- Warning detection

**Customization Points** (marked with `# TEMPLATE:`):
- Branch names (main/master/develop)
- File paths for triggering builds
- Output directories
- Project name/description

### 2. Doxygen Configuration (`Doxyfile`)

**Template for C/C++/Python/Java projects**

**Key Settings to Customize**:
```doxyfile
PROJECT_NAME           = "Your Project Name"     # Line 18
PROJECT_NUMBER         = 0.1.0                   # Line 19
PROJECT_BRIEF          = "Brief description"     # Line 20
INPUT                  = include/ src/ docs/     # Line 166 (your source paths)
STRIP_FROM_PATH        = include src             # Line 40 (for clean paths)
```

### 3. Documentation Generator Script (`scripts/generate-doxygen.py`)

**Python script to**:
- Check prerequisites (Doxygen, Graphviz)
- Generate documentation
- Create coverage reports
- Handle cross-platform paths

## 🚀 Quick Start

### For C/C++ Projects (Doxygen)

**1. Customize Doxyfile**:
```bash
# Edit Doxyfile
# Required changes:
#   - PROJECT_NAME (line 18)
#   - PROJECT_NUMBER (line 19)
#   - PROJECT_BRIEF (line 20)
#   - INPUT paths (line 166)
```

**2. Update workflow triggers**:
```yaml
# .github/workflows/documentation.yml
on:
  push:
    branches:
      - main  # Your default branch
    paths:
      - "include/**"  # Your header files
      - "src/**"      # Your source files
```

**3. Install Doxygen locally** (for testing):
```bash
# Windows
choco install doxygen.install graphviz

# Ubuntu/Debian
sudo apt-get install doxygen graphviz

# macOS
brew install doxygen graphviz
```

**4. Test locally**:
```bash
python scripts/generate-doxygen.py --check  # Verify installation
python scripts/generate-doxygen.py --clean  # Generate docs
```

**5. Commit and push** - Workflow runs automatically

### For Python Projects (Sphinx Alternative)

**Option 1: Keep Doxygen** (supports Python with `OPTIMIZE_OUTPUT_JAVA = YES`)

**Option 2: Replace with Sphinx**:

Create `.github/workflows/sphinx-docs.yml`:
```yaml
name: Sphinx Documentation

on:
  push:
    branches: [main]
    paths:
      - "src/**/*.py"
      - "docs/**"

jobs:
  build:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      
      - name: Set up Python
        uses: actions/setup-python@v5
        with:
          python-version: "3.11"
      
      - name: Install dependencies
        run: |
          pip install sphinx sphinx-rtd-theme
      
      - name: Build documentation
        run: |
          cd docs
          make html
      
      - name: Deploy to GitHub Pages
        uses: peaceiris/actions-gh-pages@v3
        with:
          github_token: ${{ secrets.GITHUB_TOKEN }}
          publish_dir: ./docs/_build/html
```

### For JavaScript/TypeScript Projects (JSDoc)

Replace Doxygen workflow with JSDoc:

Create `.github/workflows/jsdoc.yml`:
```yaml
name: JSDoc Documentation

on:
  push:
    branches: [main]
    paths:
      - "src/**/*.js"
      - "src/**/*.ts"

jobs:
  build:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      
      - name: Setup Node.js
        uses: actions/setup-node@v4
        with:
          node-version: "20"
      
      - name: Install JSDoc
        run: npm install -g jsdoc
      
      - name: Generate documentation
        run: jsdoc -c jsdoc.json -r src -d docs/jsdoc
      
      - name: Deploy to GitHub Pages
        uses: peaceiris/actions-gh-pages@v3
        with:
          github_token: ${{ secrets.GITHUB_TOKEN }}
          publish_dir: ./docs/jsdoc
```

### For Markdown Documentation (MkDocs)

Replace with MkDocs workflow:

Create `.github/workflows/mkdocs.yml`:
```yaml
name: MkDocs Documentation

on:
  push:
    branches: [main]
    paths:
      - "docs/**/*.md"
      - "mkdocs.yml"

jobs:
  deploy:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      
      - name: Setup Python
        uses: actions/setup-python@v5
        with:
          python-version: "3.11"
      
      - name: Install MkDocs
        run: |
          pip install mkdocs mkdocs-material
      
      - name: Deploy
        run: mkdocs gh-deploy --force
```

## 📋 Customization Checklist

### Workflow File (`.github/workflows/documentation.yml`)

- [ ] Update branch names (main/master)
- [ ] Customize trigger paths for your source structure
- [ ] Change project name in redirect page (line 109)
- [ ] Adjust output directories if needed
- [ ] Update Python version if required
- [ ] Configure GitHub Pages settings

### Doxyfile Configuration

- [ ] Set `PROJECT_NAME` (line 18)
- [ ] Set `PROJECT_NUMBER` (line 19)
- [ ] Set `PROJECT_BRIEF` (line 20)
- [ ] Update `INPUT` paths (line 166)
- [ ] Update `STRIP_FROM_PATH` (line 40)
- [ ] Configure `EXCLUDE` patterns (line 181)
- [ ] Set `USE_MDFILE_AS_MAINPAGE` to your README (line 197)
- [ ] Enable/disable features (graphs, diagrams, etc.)

### Python Script (`scripts/generate-doxygen.py`)

- [ ] Review prerequisite check paths
- [ ] Adjust output directory paths if needed
- [ ] Customize coverage report categories
- [ ] Add project-specific validation

## 🎨 Advanced Customization

### Custom Doxygen Styling

Create `docs/doxygen-custom.css`:
```css
/* Custom styles for your project */
body {
    font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif;
}

#nav-tree .label {
    color: #0066cc;
}
```

Add to Doxyfile:
```doxyfile
HTML_EXTRA_STYLESHEET  = docs/doxygen-custom.css
```

### Documentation Coverage Requirements

Modify workflow to enforce minimum coverage:
```yaml
- name: Check coverage threshold
  run: |
    COVERAGE=$(python scripts/generate-doxygen.py --coverage | grep "Overall:" | awk '{print $2}' | cut -d'(' -f2 | cut -d'%' -f1)
    if (( $(echo "$COVERAGE < 80.0" | bc -l) )); then
      echo "::error::Documentation coverage ($COVERAGE%) below threshold (80%)"
      exit 1
    fi
```

### Multi-Language Support

For projects with multiple languages:
```yaml
- name: Generate all documentation
  run: |
    # C++ API documentation
    doxygen Doxyfile
    
    # Python documentation
    cd python
    sphinx-build -b html docs docs/_build/html
    
    # Combine outputs
    mkdir -p docs/combined
    cp -r docs/doxygen/html docs/combined/cpp
    cp -r python/docs/_build/html docs/combined/python
```

## 🔍 Testing Documentation Locally

### Doxygen
```bash
# Generate docs
python scripts/generate-doxygen.py --clean

# Open in browser (Windows)
start docs/doxygen/html/index.html

# Open in browser (Linux/macOS)
xdg-open docs/doxygen/html/index.html  # Linux
open docs/doxygen/html/index.html      # macOS
```

### Serve with Python HTTP Server
```bash
cd docs/doxygen/html
python -m http.server 8000

# Visit http://localhost:8000
```

## 📊 Documentation Standards Compliance

### Phase 08 Requirements (ISO/IEC/IEEE 12207:2017)

- [x] **API Documentation** - Doxygen/equivalent generates from code
- [x] **User Documentation** - Include user guides in `08-transition/user-documentation/`
- [x] **Deployment Documentation** - GitHub Pages workflow included
- [x] **Version Control** - Documentation versioned with code
- [x] **Accessibility** - HTML output follows WCAG guidelines
- [x] **Traceability** - Link documentation to requirements via `@req` tags

### Documentation Quality Metrics

Track these in your workflow:
- **Coverage**: % of public APIs documented (target: >80%)
- **Warnings**: Doxygen warnings count (target: 0)
- **Completeness**: All parameters/returns documented
- **Examples**: Code examples for main features
- **Diagrams**: Class/component diagrams generated

## 🚨 Common Issues

### Workflow Fails: "Doxygen not found"
**Solution**: Ensure Doxygen is installed in the workflow (already in template)

### Warnings: "parameter 'x' not documented"
**Solution**: Add `@param` tags to function comments

### GitHub Pages not updating
**Solution**: 
1. Check repository settings → Pages → Source: `gh-pages` branch
2. Verify workflow has `contents: write` permission
3. Check workflow logs for deployment errors

### Coverage report shows 0%
**Solution**: 
1. Ensure XML output is enabled in Doxyfile (`GENERATE_XML = YES`)
2. Check that source files are in `INPUT` paths
3. Verify files have documentation comments

## 🔗 Related Documentation

- [GitHub Issue Workflow](github-issue-workflow.md) - Link issues to documentation updates
- [Phase 08 Guide](../08-transition/README.md) - Transition process requirements
- [CI/CD Workflows](ci-cd-workflows.md) - Integrate with other workflows

## 📚 External Resources

- **Doxygen**: https://www.doxygen.nl/manual/
- **Doxygen Comment Syntax**: https://www.doxygen.nl/manual/docblocks.html
- **GitHub Pages**: https://docs.github.com/en/pages
- **MkDocs**: https://www.mkdocs.org/
- **Sphinx**: https://www.sphinx-doc.org/

---

**Quick Decision Tree**:

```
What language is your project?
│
├─ C/C++/Java → Use Doxygen template (included)
│
├─ Python → Use Sphinx or Doxygen
│  └─ API docs → Doxygen
│  └─ User guides → MkDocs or Sphinx
│
├─ JavaScript/TypeScript → Replace with JSDoc
│
├─ Go → Replace with GoDoc
│
└─ Multiple languages → Combine multiple tools
```

---

**Version**: 1.0  
**Last Updated**: 2025-11-27  
**Standards**: ISO/IEC/IEEE 12207:2017 (Transition Process)
