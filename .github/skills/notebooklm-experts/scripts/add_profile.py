#!/usr/bin/env python3
"""
Interactive Profile Creator for NotebookLM Skill
Guides user through creating and authenticating a new profile with a friendly UX.
"""

import sys
import time
from pathlib import Path
from typing import Optional

# Add parent directory to path
sys.path.insert(0, str(Path(__file__).parent))

from profile_manager import ProfileManager
from auth_manager import AuthManager


def print_header(text: str, char: str = "="):
    """Print formatted header"""
    print(f"\n{char * 60}")
    print(f"{text.center(60)}")
    print(f"{char * 60}\n")


def print_step(number: int, text: str):
    """Print formatted step"""
    print(f"  [{number}] {text}")


def print_success(text: str):
    """Print success message"""
    print(f"  ✅ {text}")


def print_warning(text: str):
    """Print warning message"""
    print(f"  ⚠️  {text}")


def print_info(text: str):
    """Print info message"""
    print(f"  ℹ️  {text}")


def get_profile_name() -> str:
    """Interactively get profile name from user"""
    print_header("Create New Profile", "─")
    
    print("Examples of good profile names:")
    print("  • Work Account")
    print("  • Personal Research")
    print("  • Team Notebooks")
    print("  • Client Documentation")
    print()
    
    while True:
        name = input("📝 Enter profile name: ").strip()
        
        if not name:
            print_warning("Profile name cannot be empty")
            continue
        
        if len(name) > 50:
            print_warning("Profile name too long (max 50 characters)")
            continue
        
        # Check if profile with similar name exists
        pm = ProfileManager()
        existing = [p["name"].lower() for p in pm.get_profiles()]
        
        if name.lower() in existing:
            print_warning(f"Profile '{name}' already exists")
            continue
        
        return name


def confirm_profile_details(profile_name: str, profile_id: str) -> bool:
    """Show profile details and ask for confirmation"""
    print("\n" + "─" * 60)
    print("📋 PROFILE DETAILS")
    print("─" * 60)
    print(f"  Name: {profile_name}")
    print(f"  ID:   {profile_id}")
    print("─" * 60)
    
    while True:
        confirm = input("\n✓ Create this profile? (yes/no): ").strip().lower()
        if confirm in ["yes", "y"]:
            return True
        elif confirm in ["no", "n"]:
            return False
        else:
            print_warning("Please enter 'yes' or 'no'")


def create_profile_interactive():
    """Main interactive flow to create and authenticate a profile"""
    
    print_header("NotebookLM Profile Creator", "═")
    
    try:
        # Step 1: Get profile name
        profile_name = get_profile_name()
        
        # Step 2: Create profile
        print(f"\n⏳ Creating profile '{profile_name}'...")
        pm = ProfileManager()
        profile_entry = pm.create_profile(profile_name)
        profile_id = profile_entry["id"]
        
        print_success(f"Profile created with ID: {profile_id}")
        
        # Step 3: Confirm details
        if not confirm_profile_details(profile_name, profile_id):
            print_warning("Profile creation cancelled")
            pm.delete_profile(profile_id)
            print_success("Profile deleted")
            return False
        
        # Step 4: Setup authentication
        print_header("Google Authentication", "─")
        
        print("NEXT STEPS:")
        print_step(1, "A browser window will open automatically")
        print_step(2, "You will see the Google login page")
        print_step(3, "Sign in with your Google account")
        print_step(4, "Grant NotebookLM access (if prompted)")
        print_step(5, "You will be redirected to NotebookLM")
        
        print("\n⏱️  Timeout: 10 minutes")
        print_info("If you don't log in within 10 minutes, script will exit")
        
        input("\n🔵 Press ENTER to open browser and login...")
        
        # Setup authentication
        auth = AuthManager(profile_id=profile_id)
        success = auth.setup_auth(headless=False, timeout_minutes=10)
        
        if success:
            print_header("✅ Profile Created Successfully", "═")
            print(f"\nProfile Name: {profile_name}")
            print(f"Profile ID:   {profile_id}")
            
            # Get profile status
            pm = ProfileManager()
            profiles = pm.get_profiles()
            profile = next((p for p in profiles if p["id"] == profile_id), None)
            
            if profile:
                auth_age = time.time() - profile.get("authenticated_at", 0)
                auth_age_hours = auth_age / 3600
                print(f"Status:       ✅ AUTHENTICATED ({auth_age_hours:.1f} hours old)")
            
            print("\n" + "─" * 60)
            print("NEXT: Use this profile to query NotebookLM")
            print("─" * 60)
            print("\nQuery with this profile:")
            print(f"  .\\ run.bat ask_question.py --question \"Your question\" --profile {profile_id}")
            
            print("\nSwitch to this profile as active:")
            print(f"  .\\ run.bat auth_manager.py set-active --id {profile_id}")
            
            print("\nView all profiles:")
            print("  .\\ run.bat auth_manager.py list")
            
            print("\n" + "═" * 60)
            print("🎯 You're all set! Happy querying! 🚀")
            print("═" * 60 + "\n")
            
            return True
        else:
            print_header("❌ Authentication Failed", "═")
            print("\nPossible reasons:")
            print("  • You didn't complete login within 10 minutes")
            print("  • Browser was closed during login")
            print("  • Network connection was interrupted")
            
            print("\n" + "─" * 60)
            print("TRY AGAIN")
            print("─" * 60)
            print(f"\nRe-authenticate this profile:")
            print(f"  .\\ run.bat auth_manager.py reauth --profile {profile_id}")
            print(f"\nOr delete and create new:")
            print(f"  .\\ run.bat auth_manager.py delete --id {profile_id}")
            print(f"  python add_profile.py")
            print("\n" + "═" * 60 + "\n")
            
            return False
            
    except KeyboardInterrupt:
        print("\n\n⚠️  Operation cancelled by user")
        return False
    except Exception as e:
        print(f"\n❌ Error: {e}")
        import traceback
        traceback.print_exc()
        return False


if __name__ == "__main__":
    success = create_profile_interactive()
    sys.exit(0 if success else 1)
