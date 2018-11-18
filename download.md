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
{{ platform.title }} branches:
  {% else %}
{{ platform.title }} branches:
  {% endif %}
  {% for branch in platform.branches %}
    {% if branch.download_url %}
* [{{ branch.title}}]({{ branch.download_url }})
    {% endif %}
  {% endfor %}
{% endfor %}

Linux builds coming eventually...

[System Requirements](/faq/#system-requirements): Windows 8 or later, Vulkan compatible GPU (<a href="http://vulkan.gpuinfo.org/" target="_blank">list</a>), Intel Sandy Bridge or Haswell (AVX).

If Xenia tells you your machine is not supported, it is not supported. Don't ask how to bypass the checks.
