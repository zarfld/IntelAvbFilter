#!/usr/bin/env python3
"""
Research across multiple NotebookLM notebooks
Synthesizes information from multiple sources with citations
"""

import argparse
import sys
import json
import time
from pathlib import Path
from typing import List, Dict, Optional, Any

# Add parent directory to path
sys.path.insert(0, str(Path(__file__).parent))

from auth_manager import AuthManager
from notebook_manager import NotebookLibrary
from ask_question import ask_notebooklm


def research_topic(
    topic: str,
    notebooks: Optional[List[str]] = None,
    depth: str = "summary",
    profile_id: Optional[str] = None,
    headless: bool = True
) -> Dict[str, Any]:
    """
    Research a single topic across multiple notebooks
    
    Args:
        topic: Research question or topic
        notebooks: List of notebook IDs (None = use all in active profile)
        depth: "summary" (1 query), "comprehensive" (3 queries), "detailed" (5 queries)
        profile_id: Profile to use (None = active)
        headless: Run browser in headless mode
    
    Returns:
        dict with keys:
            - topic: str - the research question
            - findings: List[dict] - answers from each notebook with notebook_id
            - synthesis: str - AI-generated synthesis (if depth > "summary")
            - sources: List[str] - unique notebook IDs queried
            - metadata: dict - query count, duration, etc
    """
    
    auth = AuthManager(profile_id=profile_id)
    
    if not auth.is_authenticated():
        print("Error: Not authenticated. Run: auth_manager.py setup")
        return None
    
    library = NotebookLibrary(profile_id=profile_id)
    all_notebooks = library.list_notebooks()
    
    if not all_notebooks:
        print("Error: No notebooks in library. Run: notebook_manager.py add ...")
        return None
    
    # Determine which notebooks to query
    if notebooks:
        # Validate requested notebooks exist
        notebook_ids = notebooks
        query_notebooks = [nb for nb in all_notebooks if nb['id'] in notebook_ids]
        if len(query_notebooks) != len(notebook_ids):
            missing = set(notebook_ids) - {nb['id'] for nb in query_notebooks}
            print(f"Warning: Notebooks not found: {missing}")
    else:
        query_notebooks = all_notebooks if all_notebooks else []
    
    if not query_notebooks:
        print("Error: No notebooks to query")
        return None
    
    print(f"\nResearching: {topic}")
    print(f"Notebooks: {len(query_notebooks)}")
    print(f"Depth: {depth}")
    
    # Configure query count based on depth
    query_configs = {
        "summary": 1,
        "comprehensive": 3,
        "detailed": 5
    }
    query_count = query_configs.get(depth, 1)
    
    # Prepare research queries based on depth
    queries = _prepare_research_queries(topic, query_count)
    
    findings = []
    start_time = time.time()
    
    # Query each notebook
    for idx, notebook in enumerate(query_notebooks):
        print(f"\n[{idx + 1}/{len(query_notebooks)}] Querying: {notebook['name']}")
        notebook_findings = {
            "notebook_id": notebook['id'],
            "notebook_name": notebook['name'],
            "answers": [],
            "topics": notebook.get('topics', [])
        }
        
        for query_idx, query in enumerate(queries):
            print(f"  Query {query_idx + 1}/{len(queries)}: {query[:60]}...")
            
            try:
                answer = ask_notebooklm(
                    question=query,
                    notebook_url=notebook['url'],
                    headless=headless,
                    profile_id=profile_id
                )
                
                if answer:
                    notebook_findings["answers"].append({
                        "query": query,
                        "answer": answer.replace(
                            "\n\nEXTREMELY IMPORTANT: Is that ALL you need to know? ...",
                            ""  # Strip follow-up reminder for research
                        ).strip()
                    })
                    
                    # Rate limit: small delay between queries
                    if query_idx < len(queries) - 1:
                        time.sleep(2)
                else:
                    print(f"    Warning: No answer received")
            
            except Exception as e:
                print(f"    Error querying notebook: {e}")
                notebook_findings["answers"].append({
                    "query": query,
                    "error": str(e)
                })
        
        findings.append(notebook_findings)
    
    elapsed = time.time() - start_time
    
    # Synthesize findings if comprehensive or detailed
    synthesis = None
    if depth in ["comprehensive", "detailed"]:
        synthesis = _synthesize_findings(topic, findings)
    
    return {
        "topic": topic,
        "depth": depth,
        "findings": findings,
        "synthesis": synthesis,
        "sources": [nb['id'] for nb in query_notebooks],
        "metadata": {
            "notebooks_queried": len(query_notebooks),
            "queries_per_notebook": query_count,
            "total_queries": len(query_notebooks) * query_count,
            "duration_seconds": round(elapsed, 2),
            "timestamp": time.time()
        }
    }


def _prepare_research_queries(topic: str, count: int) -> List[str]:
    """Generate research queries based on topic and depth"""
    
    if count == 1:
        return [f"Please provide a comprehensive answer to: {topic}"]
    
    elif count == 3:
        return [
            f"What are the key concepts and definitions related to: {topic}?",
            f"What are the main practices, methods, or approaches for: {topic}?",
            f"What are the common challenges, limitations, or considerations for: {topic}?"
        ]
    
    elif count >= 5:
        return [
            f"Explain the fundamental concepts and definitions of: {topic}",
            f"What are the primary methods, techniques, or approaches for: {topic}?",
            f"What are the best practices and recommendations for: {topic}?",
            f"What are the common pitfalls, challenges, or things to avoid with: {topic}?",
            f"What are current trends, advancements, or future directions for: {topic}?"
        ]
    
    else:
        return [topic]


def _synthesize_findings(topic: str, findings: List[Dict]) -> str:
    """Generate synthesis summary from research findings"""
    
    synthesis_parts = [
        f"## Research Synthesis: {topic}\n",
        f"**Sources:** {len(findings)} notebooks queried\n"
    ]
    
    # Summary of findings by notebook
    for finding in findings:
        synthesis_parts.append(f"\n### {finding['notebook_name']}")
        synthesis_parts.append(f"Topics: {', '.join(finding['topics']) if finding['topics'] else 'N/A'}\n")
        
        if finding['answers']:
            synthesis_parts.append(f"Key findings from {len(finding['answers'])} queries:")
            for idx, answer_obj in enumerate(finding['answers'], 1):
                if 'error' not in answer_obj:
                    # Extract first paragraph or first 200 chars
                    answer_text = answer_obj['answer']
                    first_para = answer_text.split('\n\n')[0][:200]
                    synthesis_parts.append(f"  {idx}. {first_para}...")
    
    synthesis_parts.append("\n**Note:** This is a research summary. Review full answers for detailed information.")
    
    return "\n".join(synthesis_parts)


def export_research(research_result: Dict, output_format: str = "json") -> str:
    """
    Export research results to file
    
    Args:
        research_result: Output from research_topic()
        output_format: "json" or "markdown"
    
    Returns:
        Path to exported file
    """
    topic_slug = research_result['topic'].lower().replace(' ', '_')[:30]
    
    if output_format == "json":
        filename = f"research_{topic_slug}_{int(time.time())}.json"
        content = json.dumps(research_result, indent=2)
    else:
        filename = f"research_{topic_slug}_{int(time.time())}.md"
        content = _format_research_markdown(research_result)
    
    output_path = Path.cwd() / filename
    with open(output_path, 'w') as f:
        f.write(content)
    
    return str(output_path)


def _format_research_markdown(research_result: Dict) -> str:
    """Format research result as Markdown"""
    
    md = [
        f"# Research: {research_result['topic']}\n",
        f"**Depth:** {research_result['depth']}\n",
        f"**Notebooks:** {research_result['metadata']['notebooks_queried']}\n",
        f"**Queries:** {research_result['metadata']['total_queries']}\n",
        f"**Duration:** {research_result['metadata']['duration_seconds']}s\n",
    ]
    
    if research_result['synthesis']:
        md.append("\n## Synthesis\n")
        md.append(research_result['synthesis'])
    
    md.append("\n## Detailed Findings\n")
    
    for finding in research_result['findings']:
        md.append(f"\n### {finding['notebook_name']} ({finding['notebook_id']})\n")
        
        for idx, answer_obj in enumerate(finding['answers'], 1):
            md.append(f"\n#### Query {idx}\n")
            md.append(f"**Q:** {answer_obj.get('query', 'N/A')}\n\n")
            
            if 'error' in answer_obj:
                md.append(f"**Error:** {answer_obj['error']}\n")
            else:
                md.append(f"{answer_obj['answer']}\n")
    
    return "\n".join(md)


def main():
    """CLI interface"""
    parser = argparse.ArgumentParser(
        description="Research across multiple NotebookLM notebooks"
    )
    parser.add_argument(
        "--topics",
        nargs="+",
        required=True,
        help="Research topics/questions"
    )
    parser.add_argument(
        "--notebooks",
        nargs="*",
        help="Specific notebook IDs (None = all active profile notebooks)"
    )
    parser.add_argument(
        "--depth",
        choices=["summary", "comprehensive", "detailed"],
        default="summary",
        help="Research depth (queries per notebook)"
    )
    parser.add_argument(
        "--profile",
        help="Profile ID to use"
    )
    parser.add_argument(
        "--export",
        choices=["json", "markdown"],
        help="Export results to file"
    )
    parser.add_argument(
        "--show-browser",
        action="store_true",
        help="Show browser window (don't run headless)"
    )
    
    args = parser.parse_args()
    
    all_results = []
    
    for topic in args.topics:
        print(f"\n{'='*60}")
        result = research_topic(
            topic=topic,
            notebooks=args.notebooks if args.notebooks else None,
            depth=args.depth,
            profile_id=args.profile,
            headless=not args.show_browser
        )
        
        if result:
            all_results.append(result)
            
            # Print results
            print(f"\n{'='*60}")
            print(f"Research Complete: {topic}")
            print(f"Notebooks: {result['metadata']['notebooks_queried']}")
            print(f"Queries: {result['metadata']['total_queries']}")
            print(f"Duration: {result['metadata']['duration_seconds']}s")
            
            if result['synthesis']:
                print(f"\n{result['synthesis']}")
        else:
            print(f"Research failed for: {topic}")
    
    # Export if requested
    if args.export and all_results:
        for result in all_results:
            path = export_research(result, output_format=args.export)
            print(f"\nExported to: {path}")


if __name__ == "__main__":
    main()
