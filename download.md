---
layout: page
title: Download
icon: file_download
permalink: /download/
---

{% for platform in site.artifacts %}
  {% if platform.icon == "windows" %}
<span class="icon icon--windows" aria-hidden="true">
    <svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 88 88">
        <path d="m0 12.402 35.687-4.8602.0156 34.423-35.67.20313zm35.67 33.529.0277 34.453-35.67-4.9041-.002-29.78zm4.3261-39.025 47.318-6.906v41.527l-47.318.37565zm47.329 39.349-.0111 41.34-47.318-6.6784-.0663-34.739z" fill="#00adef"/>
    </svg>
</span>
  {% endif %}
{{ platform.title }}
  {% for branch in platform.branches %}
    {% if branch.download_url %}
* [{{ branch.name}}]({{ branch.download_url }})
    {% endif %}
  {% endfor %}
{% endfor %}

From there go to Jobs > Configuration Release > Artifacts then you can download the zip file.

Linux builds coming eventually...
