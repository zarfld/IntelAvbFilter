---
description: "Phase 09 guidance for operation and maintenance following ISO/IEC/IEEE 12207:2017. Covers system monitoring, incident response, maintenance types, and continuous improvement."
applyTo: "09-operation-maintenance/**"
---

# Phase 09: Operation & Maintenance

**Standards**: ISO/IEC/IEEE 12207:2017 (Maintenance Process, Operation Process)  
**XP Integration**: Continuous Improvement, Sustainable Pace, Collective Ownership

## 🎯 Phase Objectives

1. Operate system in production environment
2. Monitor system health and performance
3. Respond to incidents and problems
4. Perform corrective, adaptive, and perfective maintenance
5. Continuously improve based on feedback
6. Plan and execute system evolution

## 📋 Incident and Maintenance Tracking via GitHub

### Incident Tracking

Use GitHub Issues for incident management:

**Template**: Bug Report or create custom "Incident" template

**Example Incident Issue**:

**Title**: [INC-001] Login failures for users with special characters in email

**Severity**: High (P1)

**Impact**:
```markdown
- Affects: ~500 users with '+' or '.' in email addresses
- User-facing: Login fails with 400 Bad Request
- Workaround: None currently available
- Revenue impact: Potential churn for affected users
```

**Root Cause Analysis**:
```markdown
Related to #45 (REQ-F-AUTH-001: User Login)
Component: #79 (ARC-C-AUTH: Authentication Service)

**Cause**: Email validation regex doesn't handle RFC 5322 compliant addresses
**Fix**: Update validation to use proper email RFC compliance
```

**Resolution**:
```markdown
Implements fix: #PR-155
Verified by: #156 (TEST-INC-001-REGRESSION)
Deployed: v1.2.1 hotfix on 2025-11-12
```

**Labels**: `bug`, `incident`, `priority-high`, `phase-09`

### Maintenance Request Tracking

**Maintenance Types** (use labels):
- `maintenance-corrective`: Fix defects (bugs)
- `maintenance-adaptive`: Adapt to environment changes
- `maintenance-perfective`: Improve performance/features
- `maintenance-preventive`: Prevent future issues

**Example Maintenance Issue**:

**Title**: [MAINT-PERF] Optimize database queries for user search

**Type**: Perfective Maintenance

**Justification**:
```markdown
Performance degradation observed:
- User search response time: 2.5s (target: <200ms)
- Database CPU: 85% during peak hours
- N+1 query problem identified in user search endpoint
```

**Original Requirements**:
```markdown
Relates to: #46 (REQ-NF-PERF-001: Search performance)
Component: #79 (ARC-C-AUTH)
```

**Proposed Solution**:
- Add database indexes on email and name fields
- Implement query result caching (Redis, 5min TTL)
- Batch fetch related entities

**Expected Impact**:
- Response time: <200ms
- Database CPU: <50%
- Cache hit rate: >80%

### Linking Bugs to Original Requirements

Every bug MUST link back to:
```markdown
## Traceability
- **Original Requirement**: #45 (REQ-F-AUTH-001)
- **Architecture**: #79 (ARC-C-AUTH)
- **Implementation**: #PR-25 (where bug was introduced)
- **Tests**: #120 (why didn't tests catch this?)

## Impact Analysis
- Requirement still met? Partially (95% of users unaffected)
- New test needed? Yes - add edge case tests for special chars
- Documentation update? Yes - clarify supported email formats
```

## 📋 ISO/IEC/IEEE 12207:2017 Compliance

### Operation Process

1. **Operational Use** (tracked via monitoring dashboards + GitHub Issues for incidents)
   - Operate system per specifications
   - Monitor performance (metrics → alerts → issues)
   - Provide user support (support tickets → GitHub issues)
   - Log issues and feedback

2. **Operational Support** (GitHub Issues for support requests)
   - Assist users
   - Resolve operational problems
   - Maintain operational documentation

### Maintenance Process (GitHub Issues with maintenance-* labels)

1. **Maintenance Planning**
   - Define maintenance strategy
   - Allocate resources
   - Schedule maintenance activities

2. **Problem and Modification Analysis**
   - Analyze problems and requests
   - Assess impact
   - Prioritize changes

3. **Modification Implementation**
   - Design modification
   - Implement change
   - Test modification
   - Deploy update

4. **Maintenance Review and Acceptance**
   - Review modification
   - Obtain approval
   - Archive documentation

## 🎨 XP Practices for Operations & Maintenance

### Continuous Improvement
- Regular retrospectives
- Act on lessons learned
- Refactor continuously
- Improve processes

### Sustainable Pace
- No heroics or death marches
- Maintain work-life balance
- Prevent burnout
- 40-hour weeks

### Collective Ownership
- Team owns production system
- Shared on-call responsibility
- Knowledge sharing
- No "hero" mentality

## 📝 Required Deliverables

### 1. Operational Procedures
**Location**: `monitoring/operational-procedures.md`

```markdown
# Operational Procedures

## Daily Operations

### Morning Startup Checklist
Time: 8:00 AM daily

- [ ] Review overnight monitoring alerts
- [ ] Check system health dashboard
- [ ] Review error logs for anomalies
- [ ] Verify backup completion
- [ ] Check batch job completion
- [ ] Review capacity metrics
- [ ] Update operations log

### End of Day Checklist
Time: 5:00 PM daily

- [ ] Review day's incidents
- [ ] Check for pending alerts
- [ ] Verify scheduled jobs queued
- [ ] Update operations log
- [ ] Handoff to on-call engineer

## Monitoring

### Key Metrics

#### Application Metrics
| Metric | Normal Range | Warning | Critical |
|--------|-------------|---------|----------|
| Response Time (P95) | < 200ms | 200-500ms | > 500ms |
| Error Rate | < 0.1% | 0.1-1% | > 1% |
| Requests/sec | 5k-15k | 15k-20k | > 20k |
| Active Users | 1k-5k | 5k-8k | > 8k |

#### Infrastructure Metrics
| Metric | Normal Range | Warning | Critical |
|--------|-------------|---------|----------|
| CPU Usage | < 70% | 70-85% | > 85% |
| Memory Usage | < 80% | 80-90% | > 90% |
| Disk Usage | < 75% | 75-85% | > 85% |
| Network I/O | < 70% | 70-85% | > 85% |

### Monitoring Dashboards

#### Production Dashboard
URL: https://grafana.example.com/production

**Panels**:
- Request rate and latency
- Error rates by endpoint
- Active users
- Database performance
- Cache hit rate
- Queue depth

#### Infrastructure Dashboard
URL: https://grafana.example.com/infrastructure

**Panels**:
- CPU/Memory/Disk per node
- Network traffic
- Pod status (Kubernetes)
- Database connections
- Cache memory usage

### Alerts Configuration

#### Critical Alerts (Page On-Call)
- Service unavailable (>1 min)
- Error rate > 5%
- Database down
- Security incident
- Data corruption detected

#### Warning Alerts (Email/Slack)
- Error rate > 1%
- Latency > 500ms for 5 min
- CPU > 85% for 10 min
- Memory > 90% for 10 min
- Disk > 85%

## Capacity Management

### Capacity Planning
- **Weekly**: Review growth trends
- **Monthly**: Forecast next month capacity
- **Quarterly**: Plan infrastructure scaling

### Scaling Triggers

#### Auto-scaling (Horizontal)
```yaml
# Kubernetes HPA configuration
apiVersion: autoscaling/v2
kind: HorizontalPodAutoscaler
metadata:
  name: myapp-hpa
spec:
  scaleTargetRef:
    apiVersion: apps/v1
    kind: Deployment
    name: myapp
  minReplicas: 3
  maxReplicas: 50
  metrics:
  - type: Resource
    resource:
      name: cpu
      target:
        type: Utilization
        averageUtilization: 70
  - type: Resource
    resource:
      name: memory
      target:
        type: Utilization
        averageUtilization: 80
```

#### Manual Scaling (Vertical)
- CPU > 85% sustained for 1 week → Upgrade instance type
- Memory > 90% sustained for 1 week → Increase RAM
- Disk > 85% → Add storage

## Backup and Recovery

### Backup Schedule
| Component | Frequency | Retention | Verification |
|-----------|-----------|-----------|--------------|
| Database | Daily 3 AM | 30 days | Weekly restore test |
| File Storage | Daily 4 AM | 30 days | Monthly restore test |
| Configuration | On change | Forever (Git) | N/A |
| Logs | Continuous | 90 days | N/A |

### Restore Procedures

#### Database Restore
```bash
# List available backups
aws s3 ls s3://backups.example.com/database/ --recursive

# Download backup
aws s3 cp s3://backups.example.com/database/backup-2025-02-15.dump.gz ./

# Decompress
gunzip backup-2025-02-15.dump.gz

# Restore
pg_restore -h db.example.com -U admin -d mydb -c backup-2025-02-15.dump

# Verify
psql -h db.example.com -U admin -d mydb -c "SELECT COUNT(*) FROM users;"
```

**RTO**: 4 hours  
**RPO**: 24 hours (daily backups)

## Performance Optimization

### Regular Optimization Tasks

#### Weekly
- [ ] Review slow query log
- [ ] Optimize top 10 slowest queries
- [ ] Check for missing database indexes
- [ ] Review cache hit rates
- [ ] Identify and refactor performance bottlenecks

#### Monthly
- [ ] Analyze query execution plans
- [ ] Database vacuum and analyze
- [ ] Review and optimize API endpoints
- [ ] Load testing
- [ ] Capacity planning review

### Performance Tuning Checklist
- [ ] Database indexes optimized
- [ ] Query performance acceptable
- [ ] Caching strategy effective
- [ ] CDN configured properly
- [ ] Image optimization in place
- [ ] Code profiling completed
- [ ] Resource utilization balanced
```

### 2. Incident Response Procedures
**Location**: `incident-response/incident-response-playbook.md`

```markdown
# Incident Response Playbook

## Incident Severity Definitions

### P0 - Critical (Page Immediately)
- **Impact**: Service completely unavailable
- **Examples**: Total outage, data loss, security breach
- **Response Time**: Immediate
- **Resolution Target**: < 1 hour

### P1 - High (Page During Business Hours)
- **Impact**: Major functionality unavailable
- **Examples**: Core feature broken, major performance degradation
- **Response Time**: < 15 minutes
- **Resolution Target**: < 4 hours

### P2 - Medium (Assign to Queue)
- **Impact**: Minor functionality issues
- **Examples**: Non-critical feature broken, minor performance issues
- **Response Time**: < 2 hours
- **Resolution Target**: < 24 hours

### P3 - Low (Planned Work)
- **Impact**: Cosmetic or minor issues
- **Examples**: UI glitches, typos
- **Response Time**: Next business day
- **Resolution Target**: Next sprint

## Incident Response Process

```
DETECT → ASSESS → RESPOND → COMMUNICATE → RESOLVE → DOCUMENT
```

### 1. Detect
**Sources**:
- Automated monitoring alerts
- User reports
- Team member observation
- External monitoring (StatusCake, Pingdom)

### 2. Assess Severity
**Questions to ask**:
- How many users affected?
- Is core functionality impacted?
- Is data at risk?
- Are SLAs breached?
- Is security compromised?

### 3. Respond
**Immediate Actions**:
```bash
# Check service status
kubectl get pods -n production

# Check recent deployments
kubectl rollout history deployment/myapp

# Check logs
kubectl logs -n production deployment/myapp --tail=100

# Check metrics
# Visit Grafana dashboard
```

### 4. Communicate
**Update status page**: https://status.example.com

**Status Page Template**:
```
[Investigating] We are investigating reports of [issue description].
Posted: [timestamp]

[Identified] The issue has been identified as [root cause].
We are working on a fix.
Posted: [timestamp]

[Monitoring] A fix has been implemented and deployed.
We are monitoring the situation.
Posted: [timestamp]

[Resolved] The issue has been resolved.
All systems operational.
Posted: [timestamp]
```

**Stakeholder Communication**:
- **P0**: Email + SMS to all stakeholders immediately
- **P1**: Email to stakeholders within 15 minutes
- **P2/P3**: Status page updates only

### 5. Resolve

#### Common Incident Runbooks

##### Runbook: High Error Rate
**Symptoms**: Error rate > 5%, alerts firing

**Investigation**:
```bash
# Check error logs
kubectl logs -n production deployment/myapp --tail=500 | grep ERROR

# Check error rate by endpoint
# Query logging system (e.g., Elasticsearch)
curl -X POST "es.example.com:9200/logs-*/_search" -H 'Content-Type: application/json' -d'
{
  "query": {
    "range": {
      "@timestamp": {
        "gte": "now-15m"
      }
    }
  },
  "aggs": {
    "errors_by_endpoint": {
      "terms": {
        "field": "endpoint.keyword",
        "size": 10
      }
    }
  }
}'
```

**Resolution**:
1. If recent deployment → Rollback
   ```bash
   kubectl rollout undo deployment/myapp
   ```
2. If external service failing → Enable circuit breaker
3. If database issues → Check connections, restart if needed
4. If specific endpoint failing → Disable endpoint temporarily

##### Runbook: High Latency
**Symptoms**: P95 latency > 500ms

**Investigation**:
```bash
# Check database slow queries
psql -h db.example.com -U readonly -c "
  SELECT query, mean_time, calls
  FROM pg_stat_statements
  ORDER BY mean_time DESC
  LIMIT 10;"

# Check cache hit rate
redis-cli -h cache.example.com info stats

# Check resource usage
kubectl top pods -n production
```

**Resolution**:
1. Scale horizontally if CPU/Memory high
   ```bash
   kubectl scale deployment myapp --replicas=10
   ```
2. Optimize slow queries
3. Increase cache TTL
4. Enable rate limiting

##### Runbook: Service Unavailable
**Symptoms**: 503 errors, service not responding

**Investigation**:
```bash
# Check pod status
kubectl get pods -n production

# Check pod events
kubectl describe pod -n production <pod-name>

# Check node resources
kubectl top nodes
```

**Resolution**:
1. Restart unhealthy pods
   ```bash
   kubectl delete pod -n production <pod-name>
   ```
2. Scale up if resources exhausted
3. Check and fix misconfigurations
4. Rollback if deployment-related

### 6. Document

**Post-Incident Review** (within 48 hours for P0/P1):

```markdown
# Post-Incident Review: [Incident Title]

**Date**: 2025-02-15  
**Severity**: P0  
**Duration**: 45 minutes  
**Impact**: 10,000 users unable to access service  

## Timeline
- 14:32 - Alert triggered: High error rate
- 14:33 - On-call engineer paged
- 14:35 - Investigation started
- 14:42 - Root cause identified: Database connection pool exhausted
- 14:45 - Mitigation: Increased connection pool size
- 14:47 - Service recovered
- 14:52 - Monitoring confirmed resolution
- 15:17 - Incident closed

## Root Cause
Database connection pool size (default: 10) was insufficient for traffic spike (3x normal load).

## Resolution
Increased connection pool size to 50 and implemented auto-scaling based on connection usage.

## What Went Well
- Fast detection (automated alert)
- Quick response time (3 minutes)
- Clear communication on status page
- Effective rollback available (though not needed)

## What Went Wrong
- Connection pool size not configured for scale
- No alert for connection pool saturation
- No auto-scaling for database connections

## Action Items
- [ ] Implement connection pool monitoring and alerting (@engineer, 2025-02-20)
- [ ] Add connection pool sizing to capacity planning (@lead, 2025-02-22)
- [ ] Document connection pool configuration in operations manual (@engineer, 2025-02-18)
- [ ] Load test with 5x normal traffic (@qa, 2025-02-25)

## Prevention
Similar incidents will be prevented by:
1. Proper capacity planning
2. Load testing before high-traffic events
3. Connection pool monitoring
```

## Maintenance Types

### Corrective Maintenance (Bug Fixes)
**Trigger**: Production defect reported

**Process**:
1. Create incident ticket
2. Assess severity and prioritize
3. Investigate and reproduce
4. Fix in development environment
5. Write/update tests
6. Code review
7. Deploy to staging
8. Test in staging
9. Deploy to production
10. Verify fix
11. Update documentation

**SLA**:
- P0: < 1 hour
- P1: < 4 hours
- P2: < 24 hours
- P3: Next sprint

### Adaptive Maintenance (Environmental Changes)
**Examples**:
- Upgrade to new OS version
- Update dependencies (security patches)
- Migrate to new infrastructure
- API version updates

**Process**:
1. Assess impact and dependencies
2. Plan migration strategy
3. Update in development
4. Test thoroughly
5. Deploy to staging
6. Validate
7. Deploy to production (gradual rollout)
8. Monitor closely

### Perfective Maintenance (Enhancements)
**Examples**:
- Performance improvements
- Refactoring
- UX enhancements
- Feature improvements

**Process**: Follow full SDLC (Phases 01-09)

### Preventive Maintenance (Scheduled)
**Activities**:
- Database maintenance (vacuum, reindex)
- Log rotation and cleanup
- Certificate renewals
- Security scans
- Dependency updates
- Performance tuning

**Schedule**: Monthly maintenance window

## Continuous Improvement

### Metrics to Track
- **Availability**: 99.9% target
- **Mean Time to Detect (MTTD)**: < 5 minutes
- **Mean Time to Resolve (MTTR)**: < 1 hour (P0), < 4 hours (P1)
- **Deployment Frequency**: Weekly
- **Change Failure Rate**: < 5%
- **Rollback Rate**: < 2%

### Retrospectives
**Frequency**: Bi-weekly

**Format**:
1. What went well?
2. What could be improved?
3. Action items

### Knowledge Sharing
- Weekly tech talks
- Documentation reviews
- Pair programming/operations
- Incident learnings shared

## 🚨 Critical Requirements for This Phase

### Always Do
✅ Monitor system 24/7  
✅ Respond to incidents promptly  
✅ Document all incidents and resolutions  
✅ Conduct post-incident reviews (retrospectives)  
✅ Maintain backups and test restores  
✅ Keep documentation up-to-date  
✅ Practice sustainable pace (40-hour weeks, work-life balance)  
✅ Continuously improve based on feedback and lessons learned  
✅ Practice collective ownership (team owns production system)  
✅ Share on-call responsibility  
✅ Hold regular retrospectives and act on lessons learned  
✅ Refactor continuously in maintenance  
✅ Apply security patches quickly  
✅ Automate operational procedures  
✅ Focus on open, honest communication  
✅ Quality must be excellent  

### Never Do
❌ Ignore monitoring alerts  
❌ Skip post-incident reviews  
❌ Deploy without testing  
❌ Work unsustainable hours (no heroics or death marches)  
❌ Create single points of failure (knowledge silos, "hero" mentality)  
❌ Skip backup verification  
❌ Live with "broken windows" (poor designs, incorrect decisions, bad code)  
❌ Provide lame excuses (provide options instead)  
❌ Ignore defects or quality issues  
❌ Run on autopilot; constantly think critically  
❌ Allow the production of documents that aren't actively used  
❌ Skip continuous improvement activities  
❌ Prevent burnout (maintain sustainable pace)  

## 📊 Success Criteria

✅ System availability > 99.9%  
✅ MTTD < 5 minutes  
✅ MTTR < 1 hour (P0)  
✅ All incidents documented  
✅ Backups tested regularly  
✅ Team maintaining sustainable pace  
✅ Continuous improvement culture  

---

**Remember**: Operations never ends! Monitor, respond, learn, improve. Sustainable pace ensures long-term success. Collective ownership means we all care for production together.
