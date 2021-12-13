#!/usr/bin/env python3
from selenium import webdriver
from selenium.webdriver import FirefoxOptions
from webdriver_manager.firefox import GeckoDriverManager
from splinter import Browser

class google:

    def __init__(self, search_for):

        ff_options = FirefoxOptions()
        ff_options.add_argument("--headless")
        self.browser = Browser(
            headless=True, executable_path=GeckoDriverManager().install())

        self.browser.visit("https://www.google.com")
        if not self.browser.title == "Google":
            return

        self.browser.fill("q", search_for)
        self.browser.find_by_name("btnK").click()

    def links(self):
        ret = []
        for link in self.browser.find_by_tag("cite"):
            if link.value:
                ret.append(link.value)
        return ret

    def cookies(self):
        return self.browser.cookies.all()


if __name__ == "__main__":
    import sys
    results = google(sys.argv[1])
    for link in results.links():
        print(link)
