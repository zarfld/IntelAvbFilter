---
mode: agent
---

# Compile Requirements to Code

> **⚠️ UPDATED APPROACH**: This template now uses **GitHub Issues** as the single source of truth for requirements, architecture, and tests. This prompt now compiles from **GitHub Issue bodies** rather than file-based specs.
> 
> **Primary Source**: GitHub Issues (StR, REQ-F, REQ-NF, ADR, ARC-C issues)
> **Secondary Source**: Supplementary docs (only if they reference canonical issues via `#N`)
> **Deprecated Source**: ~~File-based specification files as primary artifacts~~
> 
> For GitHub Issues workflow, see:
> - Root instructions: `.github/copilot-instructions.md` (Issue-Driven Development section)
> - Phase instructions: `.github/instructions/phase-05-implementation.instructions.md`

Transform requirements from **GitHub Issues** into working code following standards and XP practices.

## 🚨 AI Agent Guardrails
**CRITICAL**: Prevent production code contamination and assumptions:
- ❌ **No stubs/simulations in PRODUCTIVE code**: Test doubles belong in test code only
- ✅ **Test mocks are acceptable**: Use dependency injection for testability
- ❌ **No "TODO" or placeholder implementations**: Complete implementations only
- ✅ **Clear test/production boundaries**: Maintain strict separation
- ❌ **No implementation-based assumptions**: Always reference GitHub Issues
- ✅ **Always trace to GitHub Issues**: Every code file must reference implementing issues in docstrings
- ✅ **All PRs link to issues**: Use "Fixes #N" or "Implements #N" in PR description

**Validation Questions**:
1. Have I validated against GitHub Issue requirements rather than assumptions?
2. Am I distinguishing between test and production code appropriately?
3. Are all implementations complete without placeholders?
4. Have I added issue traceability to code docstrings ("Implements: #N", "Architecture: #N", "Verifies: #N")?

## Objective

Compile requirements from **GitHub Issues** (REQ-F, REQ-NF, ADR, ARC-C) into production-ready code that:
- Implements all requirement issues
- Follows IEEE/ISO standards
- Applies XP practices (TDD, Simple Design, YAGNI)
- Maintains traceability via issue references in code

## Apply To

This prompt applies to compiling **GitHub Issues** into code:

**Primary Source**: GitHub Issues
- `type:requirement:functional` (REQ-F issues)
- `type:requirement:non-functional` (REQ-NF issues)
- `type:architecture:decision` (ADR issues)
- `type:architecture:component` (ARC-C issues)

**Secondary Source**: Supplementary documentation (only if referencing canonical issues)
- `docs/02-requirements/*.md` (MUST reference REQ-F/REQ-NF issues via `#N`)
- `docs/03-architecture/*.md` (MUST reference ADR/ARC-C issues via `#N`)
- `docs/04-design/*.md` (MUST reference ARC-C issues via `#N`)

## Compilation Process (GitHub Issues-Based)

### Phase 1: Analyze GitHub Issues

1. **Query GitHub Issues** for requirements
   ```bash
   # List all REQ-F issues
   gh issue list --label "type:requirement:functional" --state all
   
   # Get specific requirement details
   gh issue view 23  # REQ-F-AUTH-001: User Login
   ```

2. **Identify requirements from issue bodies**:
   - Functional requirements (REQ-F issues with label `type:requirement:functional`)
   - Non-functional requirements (REQ-NF issues with label `type:requirement:non-functional`)
   - Acceptance criteria (Given-When-Then in issue bodies)

3. **Extract design decisions from ADR issues**:
   - Architecture patterns (from ADR issue bodies)
   - Technology choices (from ADR issues)
   - Design patterns (from ARC-C issue bodies)

4. **Note constraints from REQ-NF issues**:
   - Performance requirements (REQ-NF issues)
   - Security requirements (REQ-NF issues)
   - Compliance requirements (REQ-NF issues)

### Phase 2: Plan Implementation (Issue-Driven)

1. **Break down into modules from ARC-C issues**:
   - Identify component boundaries (from ARC-C issue bodies)
   - Define interfaces (from ARC-C issues or supplementary docs referencing them)
   - Plan data flow (from ARC-C and ADR issues)

2. **Determine test strategy from TEST issues**:
   - Create TEST issue placeholders if not already created
   - Unit tests for each function (TEST issues with label `test-type:unit`)
   - Integration tests for module interactions (TEST issues with label `test-type:integration`)
   - Acceptance tests from REQ-F issue bodies (TEST issues with label `test-type:acceptance`)

3. **Identify dependencies from ADR issues**:
   - External libraries needed (documented in ADR issues)
   - Internal module dependencies (from ARC-C issues)
   - Database schema requirements (from design docs referencing ARC-C issues)

### Phase 3: Generate Tests First (TDD + Issue Traceability)

Before writing any implementation code:

1. **Create test files** for each module (reference TEST issues)
2. **Write unit tests** with issue traceability
   ```typescript
   /**
    * User authentication tests
    * 
    * Verifies: #23 (REQ-F-AUTH-001: User Login)
    * TEST Issue: #89 (TEST-AUTH-001: Authentication Tests)
    * 
    * @see https://github.com/owner/repo/issues/23
    */
   describe('UserService (Verifies #23)', () => {
     /**
      * Test valid credentials
      * Verifies acceptance criteria from REQ-F-AUTH-001 (Issue #23)
      */
     it('should authenticate user with valid credentials', async () => {
       // Test implementation
     });
     
     /**
      * Test invalid credentials
      * Verifies error handling from REQ-F-AUTH-001 (Issue #23)
      */
     it('should reject invalid credentials', async () => {
       // Test implementation
     });
   });
   ```
3. **Write integration tests** for module interactions (reference ARC-C issues)
4. **Write acceptance tests** from Given-When-Then scenarios in REQ-F issue bodies
5. **Run tests** - they should FAIL (RED)
6. **Create TEST GitHub Issues** documenting test approach if not already created

### Phase 4: Implement Code (With GitHub Issue Traceability)

1. **Write minimal code** to pass tests (GREEN)
2. **Follow standards**:
   - **IEEE 1016**: Document design decisions (reference ADR issues)
   - **Simple Design**: No unnecessary complexity
   - **YAGNI**: Build only what's specified in REQ-F issues
3. **Maintain traceability via GitHub Issue references**:
   ```typescript
   /**
    * User authentication service
    * 
    * Implements: #23 (REQ-F-AUTH-001: User Login)
    * Architecture: #45 (ARC-C-AUTH-001: Authentication Service)
    * Verified by: #89 (TEST-AUTH-001: Authentication Tests)
    * 
    * @see https://github.com/owner/repo/issues/23
    * @see https://github.com/owner/repo/issues/45
    */
   
   /**
    * Authenticates a user with email and password.
    * 
    * Implements: #23 (REQ-F-AUTH-001: User Login)
    * 
    * @param email User's email address
    * @param password User's password
    * @returns Authentication result with session token
    */
   async function authenticateUser(email: string, password: string): Promise<AuthResult> {
     // Implementation
   }
   ```
4. **Run tests** - they should PASS (GREEN)

### Phase 5: Refactor

1. **Improve code quality**:
   - Extract methods for clarity
   - Remove duplication
   - Simplify conditionals
   - Apply design patterns where appropriate
2. **Run tests** after each refactoring - keep them GREEN
3. **Update documentation** if needed

### Phase 6: Build and Verify

1. **Build the code** using VS Code tasks
   - Avoid asking user to run `npm build` manually
   - Use configured tasks
2. **Run all tests**:
   - Unit tests
   - Integration tests
   - Acceptance tests
3. **Check quality metrics**:
   - Test coverage ≥80%
   - Cyclomatic complexity ≤10
   - No linting errors
4. **Verify traceability**:
   - Every requirement implemented
   - Every implementation traced

## Code Generation Guidelines

### 1. Follow the Specification Exactly

- Implement **exactly** what's specified
- Don't add features "just in case" (YAGNI)
- Use exact argument/variable names from spec
- Follow specified error handling

### 2. Fetch Documentation

For any library used:
- Fetch the GitHub repository homepage
- Read documentation and examples
- Use library correctly as documented
- Don't assume API based on name

### 3. Preserve Specification Details

Implement all:
- Argument names
- Error messages
- Return value formats
- API endpoint paths
- Database schema
- Configuration options

### 4. Apply Design Patterns

When specification indicates:
- **"List of items"** → Iterator pattern
- **"Different algorithms"** → Strategy pattern
- **"Create objects"** → Factory pattern
- **"Single instance"** → Singleton pattern
- **"Wrap external API"** → Adapter pattern

But only if specified or clearly implied!

### 5. Handle Errors Properly

```typescript
// Good: Specific error handling from spec
if (!isValidEmail(email)) {
  throw new ValidationError('Invalid email format');
}

// Good: Spec says "log error and continue"
try {
  await processItem(item);
} catch (error) {
  logger.error('Failed to process item', { item, error });
  // Continue processing
}
```

### 6. Maintain Code Quality

- **Cyclomatic Complexity**: ≤10 per function
- **Function Length**: ≤50 lines
- **File Length**: ≤500 lines
- **Test Coverage**: ≥80%

If exceeding limits:
- Extract methods
- Split into modules
- Refactor for clarity

## Integration with CI/CD

After compilation, CI/CD pipeline will:

1. **Run linting** - code style compliance
2. **Run all tests** - unit, integration, acceptance
3. **Check coverage** - ≥80% required
4. **Validate traceability** - all requirements implemented
5. **Security scan** - no vulnerabilities
6. **Deploy to staging** - if all checks pass

## Example Workflow

### Input: Specification

```markdown
## User Authentication

**REQ-F-001**: Authenticate users via email and password

Implementation requirements:
- Use bcrypt for password hashing
- Create JWT session token on success
- Token expiry: 24 hours
- Return 401 for invalid credentials
- Log authentication attempts

### Acceptance Criteria

Given a registered user with email "user@example.com"
When they provide correct email and password
Then authentication succeeds
And a JWT token is returned
And token is valid for 24 hours

Given invalid credentials
When user attempts authentication
Then return 401 Unauthorized
And log the failed attempt
```

### Output: Test (Generated First)

```typescript
// tests/auth.test.ts
import { authenticateUser } from '../src/auth';
import { db } from '../src/database';
import bcrypt from 'bcrypt';

describe('User Authentication - REQ-F-001', () => {
  beforeEach(async () => {
    await db.clear();
    await db.users.create({
      email: 'user@example.com',
      passwordHash: await bcrypt.hash('correct-password', 10)
    });
  });

  it('should authenticate user with valid credentials', async () => {
    const result = await authenticateUser(
      'user@example.com',
      'correct-password'
    );

    expect(result.success).toBe(true);
    expect(result.token).toBeDefined();
    expect(result.expiresIn).toBe(24 * 60 * 60); // 24 hours in seconds
  });

  it('should reject invalid credentials with 401', async () => {
    await expect(
      authenticateUser('user@example.com', 'wrong-password')
    ).rejects.toThrow('Unauthorized');
  });

  it('should log failed authentication attempts', async () => {
    const logSpy = jest.spyOn(logger, 'warn');
    
    try {
      await authenticateUser('user@example.com', 'wrong-password');
    } catch {}

    expect(logSpy).toHaveBeenCalledWith(
      'Failed authentication attempt',
      expect.objectContaining({ email: 'user@example.com' })
    );
  });
});
```

### Output: Implementation (Generated After Tests)

```typescript
// src/auth.ts
import bcrypt from 'bcrypt';
import jwt from 'jsonwebtoken';
import { db } from './database';
import { logger } from './logger';

/**
 * Authenticates a user with email and password.
 * 
 * @implements REQ-F-001 - User authentication
 * @param email User's email address
 * @param password User's password (plain text)
 * @returns Authentication result with JWT token
 * @throws {Error} 'Unauthorized' if credentials are invalid (401)
 */
export async function authenticateUser(
  email: string,
  password: string
): Promise<AuthResult> {
  // Retrieve user by email
  const user = await db.users.findByEmail(email);
  
  if (!user) {
    logger.warn('Failed authentication attempt', { email, reason: 'User not found' });
    throw new Error('Unauthorized');
  }

  // Compare password with stored hash
  const passwordValid = await bcrypt.compare(password, user.passwordHash);
  
  if (!passwordValid) {
    logger.warn('Failed authentication attempt', { email, reason: 'Invalid password' });
    throw new Error('Unauthorized');
  }

  // Create JWT token with 24-hour expiry
  const token = jwt.sign(
    { userId: user.id, email: user.email },
    process.env.JWT_SECRET!,
    { expiresIn: '24h' }
  );

  logger.info('Successful authentication', { email });

  return {
    success: true,
    token,
    expiresIn: 24 * 60 * 60 // 24 hours in seconds
  };
}

interface AuthResult {
  success: boolean;
  token: string;
  expiresIn: number;
}
```

## Common Patterns

### From Specification → To Code

| Specification Pattern | Code Implementation |
|----------------------|---------------------|
| "Query items ordered by X" | `ORDER BY X` in SQL |
| "Stop when condition" | `break` in loop |
| "Save immediately" | No batching, individual `INSERT` |
| "Use package X" | Import and use library X |
| "Format: YYYY-MM-DD" | Use date formatting library |
| "Default: value" | Function parameter default |
| "Required" | Throw error if missing |
| "Optional" | Make parameter optional |
| "Validate X format" | Regex validation |
| "Log with timestamp" | Structured logging |

## Error Handling

When specification says:

- **"Return error"** → Throw appropriate exception
- **"Log error and continue"** → Try-catch with logging
- **"Retry N times"** → Implement retry logic
- **"Fail silently"** → Catch and ignore
- **"Graceful degradation"** → Fallback behavior

## Performance Optimization

Only optimize if specification requires:
- "Must complete in X seconds"
- "Handle Y concurrent requests"
- "Process Z items per second"

Otherwise, prioritize simplicity over performance (YAGNI).

## Documentation

Generate documentation that includes:

1. **API Documentation**: JSDoc/TSDoc for all public functions
2. **Architecture Diagrams**: From architecture spec
3. **Traceability**: Requirements implemented
4. **Usage Examples**: From acceptance criteria
5. **Configuration**: Environment variables, settings

## What NOT to Do

❌ **Don't**:
- Add features not in specification
- Use libraries not mentioned
- Change API signatures
- Skip tests
- Ignore error handling
- Remove traceability comments
- Modify specification files (only generate code)
- Ask user to run commands manually (use VS Code tasks)

✅ **Do**:
- Follow specification exactly
- Write tests first (TDD)
- Maintain traceability
- Apply XP practices
- Use VS Code tasks for builds
- Fetch library documentation
- Generate clean, simple code

---

**Remember**: The specification is the source of truth. Your job is to transform it into working, tested, traceable code that exactly implements what's specified—no more, no less.
